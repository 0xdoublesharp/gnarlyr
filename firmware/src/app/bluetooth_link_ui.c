#include "bluetooth_link.h"
#include "bluetooth_link_internal.h"

#include <stddef.h>

#include "core/i18n.h"

void bluetooth_link_set_enabled(BluetoothLink *link, bool enabled, uint32_t now_ms)
{
    if (link->saved.enabled != enabled) {
        link->saved.enabled = enabled;
        bluetooth_store_save(&link->saved);
    }
    if (enabled) {
        bluetooth_link_wake(link, now_ms);
        return;
    }
    hal_bluetooth_set_power(false);
    link->dozing = false;
    bluetooth_link_reset_session(link, now_ms);
}

void bluetooth_link_browse(BluetoothLink *link, bool browsing, uint32_t now_ms)
{
    link->browsing = browsing;
    link->searched = true;
    link->search_now = link->search_now && browsing;
    link->unlinked_ms = now_ms;
}

bool bluetooth_link_reconnect(BluetoothLink *link, uint32_t now_ms)
{
    if (!link->saved.enabled || !link->dozing) {
        return false;
    }
    bluetooth_link_wake(link, now_ms);
    return true;
}

static bool busy(void)
{
    HalBluetoothState state = hal_bluetooth_status()->state;
    return state == HAL_BLUETOOTH_CONNECTED || state == HAL_BLUETOOTH_CONNECTING;
}

static void aim_at(BluetoothLink *link, const HalBluetoothDevice *device, uint32_t now_ms)
{
    link->held = false;
    link->forgetting = false;
    link->search_now = false;
    link->target = *device;
    link->has_target = true;
    link->retried_ms = now_ms - BLUETOOTH_LINK_RETRY_MS;
    if (busy() || link->paired_seen) {
        hal_bluetooth_disconnect();
        link->paired_seen = false;
        link->retried_ms += BLUETOOTH_LINK_RESET_MS;
    }
}

void bluetooth_link_choose(BluetoothLink *link, int index, uint32_t now_ms)
{
    if (index < 0 || index >= link->saved.count) {
        return;
    }
    const HalBluetoothDevice *device = &link->saved.devices[index];
    if (link->connected && bluetooth_link_same_address(&link->linked, device)) {
        hal_bluetooth_disconnect();
        link->held = true;
        link->has_target = false;
        return;
    }
    aim_at(link, device, now_ms);
}

void bluetooth_link_pair(BluetoothLink *link, const HalBluetoothDevice *device, uint32_t now_ms)
{
    aim_at(link, device, now_ms);
}

void bluetooth_link_search(BluetoothLink *link, uint32_t now_ms)
{
    if (busy() || link->paired_seen) {
        hal_bluetooth_disconnect();
        link->paired_seen = false;
    }
    link->held = true;
    link->has_target = false;
    link->search_now = true;
    link->unlinked_ms = now_ms;
}

void bluetooth_link_forget(BluetoothLink *link, int index)
{
    if (index < 0 || index >= link->saved.count) {
        return;
    }
    const HalBluetoothDevice *device = &link->saved.devices[index];
    bool linked = link->connected && bluetooth_link_same_address(&link->linked, device);
    bool aimed = (link->attempting || link->has_target) &&
                 bluetooth_link_same_address(&link->target, device);
    if (linked || (aimed && (busy() || link->paired_seen))) {
        hal_bluetooth_disconnect();
        link->forgetting = linked;
        link->paired_seen = false;
    }
    if (aimed) {
        link->has_target = false;
        link->attempting = false;
    }
    if (linked) {
        link->connected = false;
    }
    for (int next = index + 1; next < link->saved.count; next++) {
        link->saved.devices[next - 1] = link->saved.devices[next];
    }
    link->saved.count--;
    bluetooth_store_save(&link->saved);
}

static bool connecting(const BluetoothLink *link)
{
    return link->attempting || link->paired_seen || link->has_target ||
           hal_bluetooth_status()->state == HAL_BLUETOOTH_CONNECTING;
}

BluetoothDeviceState bluetooth_link_device_state(const BluetoothLink *link, int index)
{
    if (index < 0 || index >= link->saved.count || !link->saved.enabled || link->dozing) {
        return BLUETOOTH_DEVICE_IDLE;
    }
    const HalBluetoothDevice *device = &link->saved.devices[index];
    if (link->connected && bluetooth_link_same_address(&link->linked, device)) {
        return link->streamed ? BLUETOOTH_DEVICE_PLAYING : BLUETOOTH_DEVICE_CONNECTED;
    }
    if (connecting(link) && bluetooth_link_same_address(&link->target, device)) {
        return BLUETOOTH_DEVICE_CONNECTING;
    }
    bool starting = hal_bluetooth_status()->state == HAL_BLUETOOTH_STARTING;
    if (starting && index == 0 && !link->held && !link->connected && !link->search_now) {
        return BLUETOOTH_DEVICE_CONNECTING;
    }
    return BLUETOOTH_DEVICE_IDLE;
}

bool bluetooth_link_searching(const BluetoothLink *link)
{
    if (!link->saved.enabled || link->dozing) {
        return false;
    }
    return link->search_now || hal_bluetooth_status()->state == HAL_BLUETOOTH_SCANNING;
}

bool bluetooth_link_knows(const BluetoothLink *link, const HalBluetoothDevice *device)
{
    return bluetooth_link_find_known(link, device) >= 0;
}

bool bluetooth_link_nearby(const BluetoothLink *link, int index)
{
    const HalBluetoothStatus *status = hal_bluetooth_status();
    if (index < 0 || index >= link->saved.count || link->connected) {
        return false;
    }
    for (uint8_t found = 0; found < status->device_count; found++) {
        if (bluetooth_link_same_address(&status->devices[found], &link->saved.devices[index])) {
            return true;
        }
    }
    return false;
}

const char *bluetooth_link_summary(const BluetoothLink *link)
{
    if (!link->saved.enabled) {
        return i18n_text(STR_OFF);
    }
    if (link->connected) {
        return link->linked.name;
    }
    if (link->dozing) {
        return i18n_text(STR_BT_NOT_CONNECTED);
    }
    HalBluetoothState state = hal_bluetooth_status()->state;
    if (state == HAL_BLUETOOTH_NO_REPLY) {
        return i18n_text(STR_BT_NOT_RESPONDING);
    }
    if (bluetooth_link_searching(link)) {
        return i18n_text(STR_BT_SEARCHING);
    }
    if (state == HAL_BLUETOOTH_STARTING) {
        return i18n_text(STR_BT_STARTING);
    }
    if (connecting(link)) {
        return i18n_text(STR_BT_CONNECTING);
    }
    return i18n_text(link->saved.count > 0 ? STR_BT_NOT_CONNECTED : STR_ON);
}
