#include "bluetooth_link.h"
#include "bluetooth_link_internal.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"

static bool same_name(const HalBluetoothDevice *first, const HalBluetoothDevice *second)
{
    for (size_t index = 0; index < HAL_BLUETOOTH_NAME_CAPACITY; index++) {
        if (first->name[index] != second->name[index]) {
            return false;
        }
        if (first->name[index] == '\0') {
            return true;
        }
    }
    return true;
}

static void announce(BluetoothLink *link, StringId pattern, const char *name, uint32_t now_ms)
{
    StringBuilder builder = string_builder_make(link->toast_text, BLUETOOTH_LINK_TOAST_CAPACITY);
    i18n_format_text(&builder, pattern, name);
    if (link->toast != NULL) {
        toast_show(link->toast, link->toast_text, now_ms);
    }
}

void bluetooth_link_reset_session(BluetoothLink *link, uint32_t now_ms)
{
    link->connected = false;
    link->streamed = false;
    link->forgetting = false;
    link->held = false;
    link->attempting = false;
    link->has_target = false;
    link->paired_seen = false;
    link->search_now = false;
    link->restarting = false;
    link->awaiting = false;
    link->searched = true;
    link->retried_ms = now_ms - BLUETOOTH_LINK_RETRY_MS;
    link->unlinked_ms = now_ms;
}

void bluetooth_link_wake(BluetoothLink *link, uint32_t now_ms)
{
    hal_bluetooth_set_power(true);
    link->dozing = false;
    link->fresh = true;
    bluetooth_link_reset_session(link, now_ms);
}

void bluetooth_link_start(BluetoothLink *link, Toast *toast, uint32_t now_ms)
{
    *link = (BluetoothLink){.toast = toast};
    bluetooth_store_load(&link->saved);
    if (link->saved.enabled) {
        bluetooth_link_wake(link, now_ms);
    }
}

void bluetooth_link_resume(BluetoothLink *link, Toast *toast, uint32_t now_ms)
{
    bool talking = hal_bluetooth_status()->state >= HAL_BLUETOOTH_READY;
    if (link->toast != toast || !link->saved.enabled || link->dozing || !talking) {
        bluetooth_link_start(link, toast, now_ms);
    }
}

void bluetooth_link_sleep(BluetoothLink *link)
{
    if (!link->saved.enabled || link->dozing) {
        return;
    }
    hal_bluetooth_set_power(false);
    link->dozing = true;
    link->connected = false;
    link->streamed = false;
    link->attempting = false;
    link->paired_seen = false;
}

static void toggle(BluetoothLink *link, Player *player)
{
    player_toggle_pause(player);
    link->player_state = (uint8_t)player->state;
}

static void remember(BluetoothLink *link, const HalBluetoothDevice *device)
{
    int known = bluetooth_link_find_known(link, device);
    if (known == 0 && same_name(&link->saved.devices[0], device)) {
        return;
    }
    int last = known >= 0 ? known : link->saved.count;
    if (last >= BLUETOOTH_STORE_DEVICES) {
        last = BLUETOOTH_STORE_DEVICES - 1;
    }
    for (int index = last; index > 0; index--) {
        link->saved.devices[index] = link->saved.devices[index - 1];
    }
    link->saved.devices[0] = *device;
    if (known < 0 && link->saved.count < BLUETOOTH_STORE_DEVICES) {
        link->saved.count++;
    }
    bluetooth_store_save(&link->saved);
}

static void follow_connection(BluetoothLink *link, const HalBluetoothStatus *status, Player *player,
                              uint32_t now_ms)
{
    bool connected = status->state == HAL_BLUETOOTH_CONNECTED;
    if (!connected) {
        link->forgetting = false;
    }
    if (connected && !link->connected && !link->forgetting) {
        link->connected = true;
        link->linked = status->connected;
        link->held = false;
        link->has_target = false;
        link->attempting = false;
        link->paired_seen = false;
        link->streamed = false;
        link->buds_paused = false;
        link->changed_ms = now_ms;
        link->seen = status->remote;
        remember(link, &status->connected);
        announce(link, STR_BT_CONNECTED_TO, status->connected.name, now_ms);
    } else if (!connected && link->connected) {
        link->connected = false;
        if (link->streamed && player->state == PLAYER_PLAYING) {
            toggle(link, player);
        }
        link->streamed = false;
        link->unlinked_ms = now_ms;
        if (!link->has_target) {
            link->retried_ms = now_ms;
        }
        announce(link, STR_BT_DISCONNECTED_FROM, link->linked.name, now_ms);
    }
}

static const HalBluetoothDevice *next_target(const BluetoothLink *link)
{
    if (link->has_target) {
        return &link->target;
    }
    if (!link->held && link->saved.count > 0) {
        return &link->saved.devices[0];
    }
    return NULL;
}

static void attempt(BluetoothLink *link, const HalBluetoothDevice *device, uint32_t now_ms)
{
    link->target = *device;
    link->has_target = false;
    link->attempting = true;
    link->searched = false;
    link->fresh = false;
    link->retried_ms = now_ms;
    hal_bluetooth_connect(&link->target);
}

static void start_scan(BluetoothLink *link, uint32_t now_ms)
{
    link->searched = true;
    link->scan_tried = false;
    if (link->fresh) {
        link->search_now = false;
        hal_bluetooth_scan();
        return;
    }
    link->search_now = true;
    link->restarting = true;
    link->restart_ms = now_ms;
    link->attempting = false;
    link->paired_seen = false;
    hal_bluetooth_set_power(false);
}

static void finish_restart(BluetoothLink *link, uint32_t now_ms)
{
    if (now_ms - link->restart_ms >= BLUETOOTH_LINK_RESTART_MS) {
        link->restarting = false;
        link->fresh = true;
        hal_bluetooth_set_power(true);
    }
}

static int found_known(const BluetoothLink *link, const HalBluetoothStatus *status)
{
    for (uint8_t index = 0; index < status->device_count; index++) {
        if ((status->found_now & 1u << index) == 0) {
            continue;
        }
        int known = bluetooth_link_find_known(link, &status->devices[index]);
        if (known >= 0) {
            return known;
        }
    }
    return -1;
}

static bool connect_found(BluetoothLink *link, const HalBluetoothStatus *status, uint32_t now_ms)
{
    int found = found_known(link, status);
    if (found < 0 || link->held || link->scan_tried) {
        return false;
    }
    link->scan_tried = true;
    attempt(link, &link->saved.devices[found], now_ms);
    return true;
}

static bool wait_for_link(BluetoothLink *link, const HalBluetoothStatus *status, uint32_t now_ms)
{
    if (status->pairing != HAL_BLUETOOTH_PAIRED) {
        link->paired_seen = false;
        return false;
    }
    if (!link->paired_seen) {
        link->paired_seen = true;
        link->paired_ms = now_ms;
    } else if (now_ms - link->paired_ms >= BLUETOOTH_LINK_CONFIRM_MS) {
        hal_bluetooth_disconnect();
        link->paired_seen = false;
        link->has_target = true;
        link->retried_ms = now_ms - BLUETOOTH_LINK_RETRY_MS + BLUETOOTH_LINK_RESET_MS;
    }
    return true;
}

static void find_device(BluetoothLink *link, const HalBluetoothStatus *status, uint32_t now_ms)
{
    if (link->connected || link->forgetting) {
        return;
    }
    if (status->state == HAL_BLUETOOTH_CONNECTING) {
        link->attempting = true;
        return;
    }
    if (status->state == HAL_BLUETOOTH_SCANNING) {
        connect_found(link, status, now_ms);
        return;
    }
    if (status->state != HAL_BLUETOOTH_READY || wait_for_link(link, status, now_ms)) {
        return;
    }
    link->attempting = false;
    if (link->search_now) {
        start_scan(link, now_ms);
        return;
    }
    if (connect_found(link, status, now_ms)) {
        return;
    }
    const HalBluetoothDevice *device = next_target(link);
    bool due = now_ms - link->retried_ms >= BLUETOOTH_LINK_RETRY_MS;
    if (device != NULL && due && (link->has_target || !link->browsing || link->searched)) {
        attempt(link, device, now_ms);
    } else if (link->browsing && (!link->searched || device == NULL)) {
        start_scan(link, now_ms);
    }
}

static uint8_t pressed(uint8_t count, uint8_t seen)
{
    return (uint8_t)(count - seen);
}

static void follow_remote(BluetoothLink *link, const HalBluetoothStatus *status, Player *player,
                          uint32_t now_ms)
{
    HalBluetoothRemote remote = status->remote;
    HalBluetoothRemote seen = link->seen;
    link->seen = remote;
    if (player->state != PLAYER_PAUSED) {
        link->buds_paused = false;
    }
    if (!link->connected) {
        return;
    }
    bool settled = now_ms - link->changed_ms >= BLUETOOTH_LINK_ECHO_MS;
    if (settled && pressed(remote.pauses, seen.pauses) > 0 && link->streamed &&
        player->state == PLAYER_PLAYING) {
        toggle(link, player);
        link->buds_paused = true;
        announce(link, STR_BT_PAUSED_BY, link->linked.name, now_ms);
    }
    if (settled && pressed(remote.plays, seen.plays) > 0 && link->buds_paused &&
        player->state == PLAYER_PAUSED) {
        toggle(link, player);
        link->buds_paused = false;
    }
    for (uint8_t count = pressed(remote.nexts, seen.nexts); count > 0; count--) {
        player_skip(player, 1);
    }
    for (uint8_t count = pressed(remote.previouses, seen.previouses); count > 0; count--) {
        player_skip(player, -1);
    }
}

static void follow_player(BluetoothLink *link, const Player *player, uint32_t now_ms)
{
    if (player->state != PLAYER_PLAYING) {
        link->wired = false;
    } else if (hal_audio_jack() != HAL_AUDIO_JACK_NONE) {
        link->wired = true;
    }
    bool stream = link->connected && player->state == PLAYER_PLAYING && !link->wired;
    if (stream != link->streamed) {
        link->streamed = stream;
        link->changed_ms = now_ms;
        hal_bluetooth_set_stream(stream);
    }
}

static void doze(BluetoothLink *link, uint32_t now_ms)
{
    if (link->connected || link->browsing || link->dozing || link->awaiting) {
        return;
    }
    if (now_ms - link->unlinked_ms >= BLUETOOTH_LINK_IDLE_OFF_MS) {
        hal_bluetooth_set_power(false);
        link->dozing = true;
        link->attempting = false;
        link->paired_seen = false;
    }
}

static bool should_wait(const BluetoothLink *link)
{
    return link->saved.on_bluetooth && link->saved.count > 0 && !link->connected && !link->held &&
           hal_audio_jack() == HAL_AUDIO_JACK_NONE;
}

static void begin_wait(BluetoothLink *link, Player *player, uint32_t now_ms)
{
    toggle(link, player);
    if (link->dozing) {
        bluetooth_link_wake(link, now_ms);
    }
    link->awaiting = true;
    link->await_ms = now_ms;
    link->unlinked_ms = now_ms;
    announce(link, STR_BT_RECONNECTING, next_target(link)->name, now_ms);
}

static void follow_wait(BluetoothLink *link, Player *player, uint32_t now_ms)
{
    if (!link->awaiting) {
        return;
    }
    bool wired = hal_audio_jack() != HAL_AUDIO_JACK_NONE;
    bool late = now_ms - link->await_ms >= BLUETOOTH_LINK_AWAIT_MS;
    if (link->connected || wired) {
        link->awaiting = false;
        if (player->state == PLAYER_PAUSED) {
            toggle(link, player);
        }
    } else if (link->held || late) {
        link->awaiting = false;
        if (!link->held) {
            announce(link, STR_BT_NOT_BACK, next_target(link)->name, now_ms);
        }
    } else if (link->toast != NULL && !link->toast->visible) {
        announce(link, STR_BT_RECONNECTING, next_target(link)->name, now_ms);
    }
}

static void note_output(BluetoothLink *link, const Player *player)
{
    bool wired = player->state == PLAYER_PLAYING && hal_audio_jack() != HAL_AUDIO_JACK_NONE;
    if ((link->streamed || wired) && link->saved.on_bluetooth != link->streamed) {
        link->saved.on_bluetooth = link->streamed;
        bluetooth_store_save(&link->saved);
    }
}

static void follow_link(BluetoothLink *link, Player *player, uint32_t now_ms)
{
    if (link->restarting) {
        finish_restart(link, now_ms);
        return;
    }
    const HalBluetoothStatus *status = hal_bluetooth_status();
    follow_connection(link, status, player, now_ms);
    follow_wait(link, player, now_ms);
    follow_remote(link, status, player, now_ms);
    follow_player(link, player, now_ms);
    find_device(link, status, now_ms);
    doze(link, now_ms);
}

void bluetooth_link_step(BluetoothLink *link, Player *player, uint32_t now_ms)
{
    if (!link->saved.enabled) {
        return;
    }
    bool started = player->state == PLAYER_PLAYING && link->player_state != PLAYER_PLAYING;
    if (player->state != link->player_state) {
        link->player_state = (uint8_t)player->state;
        link->changed_ms = now_ms;
    }
    if (started && should_wait(link)) {
        begin_wait(link, player, now_ms);
    }
    if (!link->dozing) {
        follow_link(link, player, now_ms);
    }
    note_output(link, player);
}
