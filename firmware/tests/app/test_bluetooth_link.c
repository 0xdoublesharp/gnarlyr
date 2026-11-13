#include <string.h>

#include "bluetooth_link.h"
#include "bluetooth_store.h"
#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "player.h"

enum {
    START_MS = 1000,
    BACK_IN_BLACK = 0,
    SEED = 7
};

static const HalBluetoothDevice BUDS = {.address = {1, 2, 3, 4, 5, 6}, .name = "Buds"};
static const HalBluetoothDevice SPEAKER = {.address = {9, 8, 7, 6, 5, 4}, .name = "Speaker"};

static FixtureLibrary fixture;
static Player player;
static Toast toast;
static BluetoothLink link;

static void fresh(void)
{
    fake_hal_reset();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_NONE);
    player_init(&player, fixture_library_open(&fixture));
    toast = (Toast){0};
}

static void save(bool enabled, const HalBluetoothDevice *device)
{
    BluetoothSaved saved = {.enabled = enabled, .count = device != NULL ? 1 : 0};
    if (device != NULL) {
        saved.devices[0] = *device;
    }
    bluetooth_store_save(&saved);
}

static void start(void)
{
    bluetooth_link_start(&link, &toast, START_MS);
}

static void step(uint32_t now_ms)
{
    bluetooth_link_step(&link, &player, now_ms);
}

static void play(void)
{
    BrowseQuery album = {.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = BACK_IN_BLACK,
                         .artist = LIBRARY_NONE};
    CHECK(player_play_list(&player, album, 0, SEED));
}

static void ready(void)
{
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
}

static void connect_to(const HalBluetoothDevice *device)
{
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    fake_hal_bluetooth()->connected = *device;
}

static BluetoothSaved stored(void)
{
    BluetoothSaved saved;
    bluetooth_store_load(&saved);
    return saved;
}

static bool summary_is(const char *text)
{
    return strcmp(text, bluetooth_link_summary(&link)) == 0;
}

static bool toast_is(const char *text)
{
    return toast.visible && strcmp(text, toast.text) == 0;
}

static void starts_off_when_never_turned_on(void)
{
    fresh();
    start();
    step(START_MS);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(summary_is("Off"));
}

static void turning_on_powers_the_module_and_is_remembered(void)
{
    fresh();
    start();
    bluetooth_link_set_enabled(&link, true, START_MS);
    CHECK(fake_hal_bluetooth_powered());
    CHECK(stored().enabled);
    CHECK(summary_is("Starting…"));
}

static void turning_off_powers_the_module_down(void)
{
    fresh();
    save(true, NULL);
    start();
    bluetooth_link_set_enabled(&link, false, START_MS + 100);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(!stored().enabled);
    CHECK(summary_is("Off"));
}

static void power_on_reconnects_the_saved_device(void)
{
    fresh();
    save(true, &BUDS);
    start();
    CHECK(fake_hal_bluetooth_powered());
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void a_failed_reconnect_is_retried_later(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    step(START_MS + 100);
    step(START_MS + 200);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    step(START_MS + 100 + BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
}

static void a_paired_reply_is_waited_for_without_searching(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRED;
    step(START_MS + 1000);
    step(START_MS + 100 + BLUETOOTH_LINK_CONFIRM_MS - 1);
    CHECK_EQUAL(0, fake_hal_bluetooth_scan_count());
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK_EQUAL(0, fake_hal_bluetooth_disconnect_count());
}

static void a_stalled_pairing_is_reset_and_tried_again(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRED;
    step(START_MS + 1000);
    step(START_MS + 1000 + BLUETOOTH_LINK_CONFIRM_MS - 1);
    CHECK_EQUAL(0, fake_hal_bluetooth_disconnect_count());
    uint32_t stalled_ms = START_MS + 1000 + BLUETOOTH_LINK_CONFIRM_MS;
    step(stalled_ms);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_NONE;
    step(stalled_ms + BLUETOOTH_LINK_RESET_MS - 1);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    step(stalled_ms + BLUETOOTH_LINK_RESET_MS);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void restart_for_search(uint32_t failed_ms)
{
    step(failed_ms);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(summary_is("Searching…"));
    step(failed_ms + BLUETOOTH_LINK_RESTART_MS - 1);
    CHECK(!fake_hal_bluetooth_powered());
    step(failed_ms + BLUETOOTH_LINK_RESTART_MS);
    CHECK(fake_hal_bluetooth_powered());
    CHECK_EQUAL(HAL_BLUETOOTH_STARTING, fake_hal_bluetooth()->state);
    CHECK(summary_is("Searching…"));
    CHECK_EQUAL(BLUETOOTH_DEVICE_IDLE, bluetooth_link_device_state(&link, 0));
    ready();
    step(failed_ms + BLUETOOTH_LINK_RESTART_MS + 100);
}

static void a_search_and_a_reconnect_take_turns(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_FAILED;
    restart_for_search(START_MS + 200);
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    CHECK_EQUAL(HAL_BLUETOOTH_SCANNING, fake_hal_bluetooth()->state);
    step(START_MS + 100 + 2 * BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(summary_is("Searching…"));
    ready();
    step(START_MS + 100 + 2 * BLUETOOTH_LINK_RETRY_MS + 100);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
}

static void the_first_search_after_power_on_needs_no_restart(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    bluetooth_link_search(&link, START_MS + 50);
    ready();
    step(START_MS + 100);
    CHECK(fake_hal_bluetooth_powered());
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    CHECK_EQUAL(0, fake_hal_bluetooth_connect_count());
}

static void pair_new_device_after_a_connect_attempt_restarts_the_module(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTING;
    step(START_MS + 200);
    bluetooth_link_search(&link, START_MS + 300);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    ready();
    restart_for_search(START_MS + 400);
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
}

static void a_device_chosen_during_a_restart_connects_once_the_module_is_ready(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_FAILED;
    step(START_MS + 200);
    CHECK(!fake_hal_bluetooth_powered());
    bluetooth_link_pair(&link, &SPEAKER, START_MS + 300);
    CHECK(summary_is("Connecting…"));
    step(START_MS + 200 + BLUETOOTH_LINK_RESTART_MS);
    ready();
    step(START_MS + 300 + BLUETOOTH_LINK_RESTART_MS);
    CHECK_EQUAL(0, fake_hal_bluetooth_scan_count());
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Speaker", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void leaving_the_screen_during_a_restart_drops_the_search(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_FAILED;
    step(START_MS + 200);
    CHECK(!fake_hal_bluetooth_powered());
    bluetooth_link_browse(&link, false, START_MS + 300);
    step(START_MS + 200 + BLUETOOTH_LINK_RESTART_MS);
    CHECK(fake_hal_bluetooth_powered());
    ready();
    step(START_MS + 300 + BLUETOOTH_LINK_RESTART_MS);
    CHECK_EQUAL(0, fake_hal_bluetooth_scan_count());
    step(START_MS + 100 + BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
}

static void turning_bluetooth_off_during_a_restart_keeps_it_off(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_FAILED;
    step(START_MS + 200);
    bluetooth_link_set_enabled(&link, false, START_MS + 300);
    step(START_MS + 300 + BLUETOOTH_LINK_RESTART_MS);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(summary_is("Off"));
}

static void nothing_is_retried_while_connecting(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    step(START_MS + 100);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTING;
    step(START_MS + 100 + BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
}

static void connecting_saves_the_device_and_says_so(void)
{
    fresh();
    save(true, NULL);
    start();
    connect_to(&SPEAKER);
    step(START_MS + 100);
    BluetoothSaved saved = stored();
    CHECK_EQUAL(1, saved.count);
    CHECK(strcmp("Speaker", saved.devices[0].name) == 0);
    CHECK(toast_is("Connected to Speaker"));
    CHECK(summary_is("Speaker"));
}

static void resuming_keeps_a_live_connection(void)
{
    fresh();
    save(true, NULL);
    start();
    connect_to(&SPEAKER);
    step(START_MS + 100);
    bluetooth_link_resume(&link, &toast, START_MS + 60000);
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTED, fake_hal_bluetooth()->state);
    CHECK(summary_is("Speaker"));
}

static void resuming_wakes_a_module_that_is_off(void)
{
    fresh();
    save(true, NULL);
    start();
    hal_bluetooth_set_power(false);
    bluetooth_link_resume(&link, &toast, START_MS + 100);
    CHECK(fake_hal_bluetooth_powered());
}

static void playing_with_nothing_plugged_in_streams(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    CHECK(!fake_hal_bluetooth()->streaming);
    play();
    step(START_MS + 200);
    CHECK(fake_hal_bluetooth()->streaming);
    player_toggle_pause(&player);
    step(START_MS + 300);
    CHECK(!fake_hal_bluetooth()->streaming);
}

static void headphones_take_the_sound_back(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    CHECK(fake_hal_bluetooth()->streaming);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    step(START_MS + 200);
    CHECK(!fake_hal_bluetooth()->streaming);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
}

static void the_buds_wait_for_the_next_play_after_headphones(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    step(START_MS + 100);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_NONE);
    step(START_MS + 200);
    CHECK(!fake_hal_bluetooth()->streaming);
    player_toggle_pause(&player);
    step(START_MS + 300);
    player_toggle_pause(&player);
    step(START_MS + 400);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void the_buds_pause_and_resume_playback(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    fake_hal_bluetooth()->remote.pauses++;
    step(START_MS + 100 + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(!fake_hal_bluetooth()->streaming);
    fake_hal_bluetooth()->remote.plays++;
    step(START_MS + 100 + 2 * BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void the_buds_resume_a_pause_that_went_to_standby_at_its_place(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    fake_hal_audio_set_position(42000);
    fake_hal_bluetooth()->remote.pauses++;
    step(START_MS + 100 + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    uint32_t paused_ms = START_MS + 1000;
    player_update(&player, paused_ms);
    player_update(&player, paused_ms + PLAYER_PAUSE_RELEASE_MS);
    CHECK(player_in_standby(&player));
    step(paused_ms + PLAYER_PAUSE_RELEASE_MS);
    CHECK(link.connected);
    fake_hal_bluetooth()->remote.plays++;
    step(paused_ms + PLAYER_PAUSE_RELEASE_MS + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
    CHECK(fake_hal_bluetooth()->streaming);
}

static void the_buds_resume_a_player_pause_from_standby(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    fake_hal_audio_set_position(42000);
    player_toggle_pause(&player);
    step(START_MS + 100 + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    uint32_t paused_ms = START_MS + 1000;
    player_update(&player, paused_ms);
    player_update(&player, paused_ms + PLAYER_PAUSE_RELEASE_MS);
    CHECK(player_in_standby(&player));
    step(paused_ms + PLAYER_PAUSE_RELEASE_MS);
    CHECK(link.connected);
    fake_hal_bluetooth()->remote.plays++;
    step(paused_ms + PLAYER_PAUSE_RELEASE_MS + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
    CHECK(fake_hal_bluetooth()->streaming);
}

static void a_pause_report_right_after_play_is_ignored(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    player_toggle_pause(&player);
    step(START_MS + 100 + BLUETOOTH_LINK_ECHO_MS);
    player_toggle_pause(&player);
    fake_hal_bluetooth()->remote.pauses++;
    step(START_MS + 200 + 2 * BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(fake_hal_bluetooth()->streaming);
    CHECK(!toast.visible || strcmp("Paused by Buds", toast.text) != 0);
}

static void a_pause_from_the_buds_says_so(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    fake_hal_bluetooth()->remote.pauses++;
    step(START_MS + 100 + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(toast_is("Paused by Buds"));
}

static void reports_right_after_the_stream_starts_are_ignored(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    fake_hal_bluetooth()->remote.pauses++;
    step(START_MS + 200);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void a_settled_play_report_resumes_what_the_player_paused(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    player_toggle_pause(&player);
    step(START_MS + 200);
    fake_hal_bluetooth()->remote.plays++;
    step(START_MS + 200 + BLUETOOTH_LINK_ECHO_MS);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
}

static void the_buds_skip_tracks(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    uint16_t first = player.track;
    fake_hal_bluetooth()->remote.nexts++;
    step(START_MS + 200);
    CHECK(player.track != first);
    fake_hal_bluetooth()->remote.previouses++;
    step(START_MS + 300);
    CHECK_EQUAL(first, player.track);
}

static void presses_from_before_the_connection_are_ignored(void)
{
    fresh();
    save(true, &BUDS);
    start();
    play();
    fake_hal_bluetooth()->remote.nexts = 3;
    uint16_t first = player.track;
    connect_to(&BUDS);
    step(START_MS + 100);
    CHECK_EQUAL(first, player.track);
}

static void losing_the_buds_pauses_playback(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    ready();
    step(START_MS + 200);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(toast_is("Disconnected from Buds"));
}

static void an_unused_module_switches_itself_off(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    step(START_MS + BLUETOOTH_LINK_IDLE_OFF_MS - 1);
    CHECK(fake_hal_bluetooth_powered());
    step(START_MS + BLUETOOTH_LINK_IDLE_OFF_MS);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(stored().enabled);
    CHECK(summary_is("Not connected"));
}

static void browsing_leaves_a_dozing_module_off_until_asked(void)
{
    fresh();
    save(true, &BUDS);
    start();
    step(START_MS + BLUETOOTH_LINK_IDLE_OFF_MS);
    CHECK(!fake_hal_bluetooth_powered());
    bluetooth_link_browse(&link, true, START_MS + BLUETOOTH_LINK_IDLE_OFF_MS + 100);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(bluetooth_link_reconnect(&link, START_MS + BLUETOOTH_LINK_IDLE_OFF_MS + 200));
    CHECK(fake_hal_bluetooth_powered());
    CHECK(!bluetooth_link_reconnect(&link, START_MS + BLUETOOTH_LINK_IDLE_OFF_MS + 300));
    step(START_MS + 3 * BLUETOOTH_LINK_IDLE_OFF_MS);
    CHECK(fake_hal_bluetooth_powered());
}

static void browsing_searches_for_devices(void)
{
    fresh();
    save(true, NULL);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    CHECK(summary_is("Searching…"));
}

static void a_search_that_finds_remembered_headphones_connects_them(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {SPEAKER, BUDS}};
    bluetooth_store_save(&two);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_FAILED;
    restart_for_search(START_MS + 200);
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    fake_hal_bluetooth()->devices[0] = BUDS;
    fake_hal_bluetooth()->device_count = 1;
    fake_hal_bluetooth()->found_now = 1;
    step(START_MS + 1000);
    CHECK_EQUAL(0, fake_hal_bluetooth_stop_count());
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_FAILED;
    ready();
    step(START_MS + 1100);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
}

static void an_old_search_result_does_not_interrupt_a_new_search(void)
{
    fresh();
    save(true, &BUDS);
    start();
    bluetooth_link_browse(&link, true, START_MS);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_SCANNING;
    fake_hal_bluetooth()->devices[0] = BUDS;
    fake_hal_bluetooth()->device_count = 1;
    fake_hal_bluetooth()->found_now = 0;
    step(START_MS + 100);
    CHECK_EQUAL(0, fake_hal_bluetooth_connect_count());
}

static void after_pair_new_device_remembered_headphones_only_show_as_nearby(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    bluetooth_link_search(&link, START_MS + 200);
    ready();
    step(START_MS + 300);
    fake_hal_bluetooth()->devices[0] = BUDS;
    fake_hal_bluetooth()->device_count = 1;
    ready();
    step(START_MS + 400);
    CHECK_EQUAL(0, fake_hal_bluetooth_connect_count());
    CHECK(bluetooth_link_nearby(&link, 0));
}

static void pairing_connects_the_chosen_device(void)
{
    fresh();
    save(true, NULL);
    start();
    ready();
    bluetooth_link_pair(&link, &SPEAKER, START_MS + 100);
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Speaker", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void devices_are_remembered_most_recent_first(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&SPEAKER);
    step(START_MS + 100);
    BluetoothSaved saved = stored();
    CHECK_EQUAL(2, saved.count);
    CHECK(strcmp("Speaker", saved.devices[0].name) == 0);
    CHECK(strcmp("Buds", saved.devices[1].name) == 0);
}

static void choosing_the_connected_device_disconnects_and_stays_off(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    bluetooth_link_choose(&link, 0, START_MS + 200);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    ready();
    step(START_MS + 300);
    step(START_MS + 300 + 3 * BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(0, fake_hal_bluetooth_connect_count());
    CHECK(summary_is("Not connected"));
    bluetooth_link_choose(&link, 0, START_MS + 400 + 3 * BLUETOOTH_LINK_RETRY_MS);
    step(START_MS + 400 + 3 * BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
}

static void choosing_another_device_switches_to_it(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {BUDS, SPEAKER}};
    bluetooth_store_save(&two);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    bluetooth_link_choose(&link, 1, START_MS + 200);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTING, bluetooth_link_device_state(&link, 1));
    ready();
    step(START_MS + 300);
    step(START_MS + 200 + BLUETOOTH_LINK_RESET_MS);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Speaker", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void searching_disconnects_and_scans_without_reconnecting(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    bluetooth_link_search(&link, START_MS + 200);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    ready();
    step(START_MS + 300);
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    ready();
    step(START_MS + 300 + 2 * BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(0, fake_hal_bluetooth_connect_count());
}

static void a_store_of_another_version_loads_nothing(void)
{
    fresh();
    uint8_t record[3 + 1 + 1 + 6 * 38 + 1] = {'B', 'T', 2, 1,   1,   1,   2,  3,
                                              4,   5,   6, 'B', 'u', 'd', 's'};
    fake_hal_add_file(BLUETOOTH_STORE_PATH, record, sizeof record);
    BluetoothSaved saved = stored();
    CHECK(!saved.enabled);
    CHECK_EQUAL(0, saved.count);
    record[2] = 1;
    saved = stored();
    CHECK(saved.enabled);
    CHECK_EQUAL(1, saved.count);
    CHECK(strcmp("Buds", saved.devices[0].name) == 0);
}

static void invalid_saved_device_indices_leave_the_connection_and_store_alone(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {BUDS, SPEAKER}};
    bluetooth_store_save(&two);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    const int invalid[] = {-1, 2, 100};
    for (uint32_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        bluetooth_link_choose(&link, invalid[index], START_MS + 200);
        bluetooth_link_forget(&link, invalid[index]);
        CHECK_EQUAL(BLUETOOTH_DEVICE_IDLE, bluetooth_link_device_state(&link, invalid[index]));
        CHECK(!bluetooth_link_nearby(&link, invalid[index]));
    }
    BluetoothSaved saved = stored();
    CHECK_EQUAL(2, saved.count);
    CHECK(strcmp("Buds", saved.devices[0].name) == 0);
    CHECK(strcmp("Speaker", saved.devices[1].name) == 0);
    CHECK_EQUAL(0, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTED, bluetooth_link_device_state(&link, 0));
}

static void forgetting_a_pending_connection_cancels_it_without_retrying(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTING;
    bluetooth_link_forget(&link, 0);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(0, stored().count);
    ready();
    step(START_MS + 200 + 2 * BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK_EQUAL(0, stored().count);
}

static void forgetting_an_unconnected_device_keeps_the_other_connection(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {BUDS, SPEAKER}};
    bluetooth_store_save(&two);
    start();
    connect_to(&SPEAKER);
    step(START_MS + 100);
    bluetooth_link_forget(&link, 1);
    BluetoothSaved saved = stored();
    CHECK_EQUAL(1, saved.count);
    CHECK(strcmp("Speaker", saved.devices[0].name) == 0);
    CHECK_EQUAL(0, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTED, bluetooth_link_device_state(&link, 0));
}

static void forgetting_the_first_device_preserves_and_retries_the_next_one(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {BUDS, SPEAKER}};
    bluetooth_store_save(&two);
    start();
    ready();
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTING;
    bluetooth_link_forget(&link, 0);
    BluetoothSaved saved = stored();
    CHECK_EQUAL(1, saved.count);
    CHECK(strcmp("Speaker", saved.devices[0].name) == 0);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    ready();
    step(START_MS + 200 + 2 * BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Speaker", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void connecting_without_a_toast_still_remembers_the_device(void)
{
    fresh();
    save(true, NULL);
    bluetooth_link_start(&link, NULL, START_MS);
    connect_to(&BUDS);
    step(START_MS + 100);
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTED, bluetooth_link_device_state(&link, 0));
    CHECK_EQUAL(1, stored().count);
    CHECK(bluetooth_link_knows(&link, &BUDS));
}

static void a_remembered_device_updates_its_name_when_it_reconnects(void)
{
    fresh();
    save(true, &BUDS);
    start();
    HalBluetoothDevice renamed = BUDS;
    memcpy(renamed.name, "New name", sizeof "New name");
    connect_to(&renamed);
    step(START_MS + 100);
    BluetoothSaved saved = stored();
    CHECK_EQUAL(1, saved.count);
    CHECK(strcmp("New name", saved.devices[0].name) == 0);
    CHECK(bluetooth_link_knows(&link, &BUDS));
}

static void sleeping_an_enabled_module_is_idempotent_and_resume_wakes_it(void)
{
    fresh();
    start();
    bluetooth_link_sleep(&link);
    CHECK(!fake_hal_bluetooth_powered());
    bluetooth_link_set_enabled(&link, true, START_MS);
    bluetooth_link_sleep(&link);
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(stored().enabled);
    CHECK(!bluetooth_link_searching(&link));
    bluetooth_link_sleep(&link);
    step(START_MS + BLUETOOTH_LINK_RETRY_MS);
    CHECK(!fake_hal_bluetooth_powered());
    bluetooth_link_resume(&link, &toast, START_MS + BLUETOOTH_LINK_RETRY_MS);
    CHECK(fake_hal_bluetooth_powered());
}

static void choosing_a_different_device_during_paired_confirmation_resets_the_old_one(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {BUDS, SPEAKER}};
    bluetooth_store_save(&two);
    start();
    ready();
    step(START_MS + 100);
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRED;
    step(START_MS + 200);
    bluetooth_link_choose(&link, 1, START_MS + 300);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTING, bluetooth_link_device_state(&link, 1));
    fake_hal_bluetooth()->pairing = HAL_BLUETOOTH_PAIRING_NONE;
    ready();
    step(START_MS + 300 + BLUETOOTH_LINK_RESET_MS - 1);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    step(START_MS + 300 + BLUETOOTH_LINK_RESET_MS);
    CHECK_EQUAL(2, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Speaker", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void a_seventh_device_evicts_only_the_least_recent_saved_device(void)
{
    fresh();
    BluetoothSaved full = {.enabled = true, .count = BLUETOOTH_STORE_DEVICES};
    for (uint32_t index = 0; index < BLUETOOTH_STORE_DEVICES; index++) {
        full.devices[index] = BUDS;
        full.devices[index].address[0] = (uint8_t)(20 + index);
    }
    bluetooth_store_save(&full);
    start();
    connect_to(&SPEAKER);
    step(START_MS + 100);
    BluetoothSaved saved = stored();
    CHECK_EQUAL(BLUETOOTH_STORE_DEVICES, saved.count);
    CHECK(strcmp("Speaker", saved.devices[0].name) == 0);
    for (uint32_t index = 1; index < BLUETOOTH_STORE_DEVICES; index++) {
        CHECK_EQUAL(full.devices[index - 1].address[0], saved.devices[index].address[0]);
    }
    CHECK(!bluetooth_link_knows(&link, &full.devices[BLUETOOTH_STORE_DEVICES - 1]));
    CHECK(bluetooth_link_knows(&link, &SPEAKER));
}

static void discovery_skips_unknown_devices_and_connects_the_known_match(void)
{
    fresh();
    save(true, &BUDS);
    start();
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_SCANNING;
    fake_hal_bluetooth()->devices[0] = SPEAKER;
    fake_hal_bluetooth()->devices[1] = BUDS;
    fake_hal_bluetooth()->device_count = 2;
    fake_hal_bluetooth()->found_now = 3;
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void a_newly_found_saved_device_can_connect_while_ready(void)
{
    fresh();
    save(true, &BUDS);
    start();
    ready();
    fake_hal_bluetooth()->devices[0] = BUDS;
    fake_hal_bluetooth()->device_count = 1;
    fake_hal_bluetooth()->found_now = 1;
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void forgetting_another_saved_device_keeps_the_chosen_target(void)
{
    fresh();
    BluetoothSaved two = {.enabled = true, .count = 2, .devices = {BUDS, SPEAKER}};
    bluetooth_store_save(&two);
    start();
    ready();
    bluetooth_link_choose(&link, 0, START_MS + 100);
    bluetooth_link_forget(&link, 1);
    CHECK_EQUAL(0, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(1, stored().count);
    step(START_MS + 100);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
}

static void forgetting_disconnects_and_clears_the_device(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    bluetooth_link_forget(&link, 0);
    CHECK_EQUAL(1, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(0, stored().count);
    CHECK(stored().enabled);
    step(START_MS + 200);
    CHECK_EQUAL(0, stored().count);
    CHECK(summary_is("On"));
}

static void save_on_bluetooth(const HalBluetoothDevice *device)
{
    BluetoothSaved saved = {
        .enabled = true, .count = 1, .on_bluetooth = true, .devices = {*device}};
    bluetooth_store_save(&saved);
}

static void play_after_bluetooth_switched_itself_off_reconnects_and_then_plays(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    ready();
    uint32_t dozed_ms = START_MS + BLUETOOTH_LINK_IDLE_OFF_MS;
    step(dozed_ms);
    CHECK(!fake_hal_bluetooth_powered());
    int tries = fake_hal_bluetooth_connect_count();
    play();
    step(dozed_ms + 100);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(fake_hal_bluetooth_powered());
    CHECK(toast_is("Connecting to Buds…"));
    ready();
    step(dozed_ms + 200);
    CHECK_EQUAL(tries + 1, fake_hal_bluetooth_connect_count());
    CHECK(strcmp("Buds", fake_hal_bluetooth_connect_target()->name) == 0);
    connect_to(&BUDS);
    step(dozed_ms + 3000);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void play_right_after_power_on_waits_for_the_last_headphones(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    play();
    step(START_MS + 100);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(toast_is("Connecting to Buds…"));
    connect_to(&BUDS);
    step(START_MS + 2000);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void headphones_that_stay_away_leave_the_player_paused(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    play();
    step(START_MS + 100);
    ready();
    step(START_MS + 100 + BLUETOOTH_LINK_AWAIT_MS - 1);
    CHECK(toast_is("Connecting to Buds…"));
    step(START_MS + 100 + BLUETOOTH_LINK_AWAIT_MS);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(toast_is("Buds didn't connect · Play: retry"));
    connect_to(&BUDS);
    step(START_MS + 200 + BLUETOOTH_LINK_AWAIT_MS);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(!fake_hal_bluetooth()->streaming);
}

static void play_during_the_wait_waits_again(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    play();
    step(START_MS + 100);
    player_toggle_pause(&player);
    step(START_MS + 100 + BLUETOOTH_LINK_AWAIT_MS - 100);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    step(START_MS + 100 + BLUETOOTH_LINK_AWAIT_MS);
    CHECK(toast_is("Connecting to Buds…"));
    connect_to(&BUDS);
    step(START_MS + 200 + BLUETOOTH_LINK_AWAIT_MS);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
}

static void wired_headphones_play_at_once_instead_of_waiting(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    play();
    step(START_MS + 100);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(!toast.visible);
    CHECK(!stored().on_bluetooth);
}

static void plugging_in_while_waiting_plays_on_the_wired_headphones(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    play();
    step(START_MS + 100);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_BALANCED);
    step(START_MS + 200);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(!fake_hal_bluetooth()->streaming);
}

static void music_last_played_on_the_jack_starts_without_waiting(void)
{
    fresh();
    save(true, &BUDS);
    start();
    play();
    step(START_MS + 100);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK(!toast.visible);
}

static void the_output_music_last_used_is_remembered(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    CHECK(stored().on_bluetooth);
    player_toggle_pause(&player);
    step(START_MS + 200);
    CHECK(stored().on_bluetooth);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    player_toggle_pause(&player);
    step(START_MS + 300);
    CHECK(!stored().on_bluetooth);
    CHECK(stored().enabled);
    CHECK_EQUAL(1, stored().count);
}

static void headphones_disconnected_on_purpose_are_not_waited_for(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    connect_to(&BUDS);
    step(START_MS + 100);
    bluetooth_link_choose(&link, 0, START_MS + 200);
    ready();
    step(START_MS + 300);
    play();
    step(START_MS + 400);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
}

static void headphones_back_in_range_reconnect_without_playing(void)
{
    fresh();
    save(true, &BUDS);
    start();
    connect_to(&BUDS);
    play();
    step(START_MS + 100);
    ready();
    step(START_MS + 200);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    step(START_MS + 200 + BLUETOOTH_LINK_RETRY_MS);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    connect_to(&BUDS);
    step(START_MS + 300 + BLUETOOTH_LINK_RETRY_MS);
    CHECK(link.connected);
    CHECK_EQUAL(PLAYER_PAUSED, player.state);
    CHECK(!link.streamed);
    CHECK(toast_is("Connected to Buds"));
    player_toggle_pause(&player);
    step(START_MS + 400 + BLUETOOTH_LINK_RETRY_MS);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void a_wait_keeps_the_module_awake(void)
{
    fresh();
    save_on_bluetooth(&BUDS);
    start();
    ready();
    step(START_MS + BLUETOOTH_LINK_IDLE_OFF_MS - 100);
    play();
    step(START_MS + BLUETOOTH_LINK_IDLE_OFF_MS - 50);
    step(START_MS + BLUETOOTH_LINK_IDLE_OFF_MS + 100);
    CHECK(fake_hal_bluetooth_powered());
}

static void a_short_or_out_of_range_store_loads_nothing(void)
{
    fresh();
    uint8_t record[3 + 1 + 1 + 6 * 38 + 1] = {'B', 'T', 1, 1,   1,   1,   2,  3,
                                              4,   5,   6, 'B', 'u', 'd', 's'};
    fake_hal_add_file(BLUETOOTH_STORE_PATH, record, sizeof record - 1);
    CHECK_EQUAL(0, stored().count);
    static const uint8_t FIELDS[][2] = {{3, 2}, {4, 7}, {sizeof record - 1, 2}};
    for (uint32_t at = 0; at < sizeof FIELDS / sizeof FIELDS[0]; at++) {
        fresh();
        fake_hal_add_file(BLUETOOTH_STORE_PATH, record, sizeof record);
        CHECK_EQUAL(1, stored().count);
        uint8_t kept = record[FIELDS[at][0]];
        record[FIELDS[at][0]] = FIELDS[at][1];
        CHECK_EQUAL(0, stored().count);
        record[FIELDS[at][0]] = kept;
    }
}

static void device_state_classifies_each_transient_condition(void)
{
    fresh();
    link = (BluetoothLink){.saved = {.enabled = false, .count = 1, .devices = {BUDS}}};
    CHECK_EQUAL(BLUETOOTH_DEVICE_IDLE, bluetooth_link_device_state(&link, 0));
    link.saved.enabled = true;
    link.dozing = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_IDLE, bluetooth_link_device_state(&link, 0));
    link.dozing = false;
    link.connected = true;
    link.linked = BUDS;
    link.streamed = false;
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTED, bluetooth_link_device_state(&link, 0));
    link.streamed = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_PLAYING, bluetooth_link_device_state(&link, 0));
    link.connected = false;
    link.streamed = false;
    link.target = BUDS;
    link.attempting = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTING, bluetooth_link_device_state(&link, 0));
    link.attempting = false;
    link.paired_seen = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTING, bluetooth_link_device_state(&link, 0));
    link.paired_seen = false;
    link.has_target = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTING, bluetooth_link_device_state(&link, 0));
    link.has_target = false;
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_STARTING;
    CHECK_EQUAL(BLUETOOTH_DEVICE_CONNECTING, bluetooth_link_device_state(&link, 0));
    link.held = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_IDLE, bluetooth_link_device_state(&link, 0));
    link.held = false;
    link.search_now = true;
    CHECK_EQUAL(BLUETOOTH_DEVICE_IDLE, bluetooth_link_device_state(&link, 0));
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(starts_off_when_never_turned_on),
        TEST_CASE(turning_on_powers_the_module_and_is_remembered),
        TEST_CASE(turning_off_powers_the_module_down),
        TEST_CASE(power_on_reconnects_the_saved_device),
        TEST_CASE(a_failed_reconnect_is_retried_later),
        TEST_CASE(a_paired_reply_is_waited_for_without_searching),
        TEST_CASE(a_stalled_pairing_is_reset_and_tried_again),
        TEST_CASE(nothing_is_retried_while_connecting),
        TEST_CASE(resuming_keeps_a_live_connection),
        TEST_CASE(resuming_wakes_a_module_that_is_off),
        TEST_CASE(a_search_and_a_reconnect_take_turns),
        TEST_CASE(the_first_search_after_power_on_needs_no_restart),
        TEST_CASE(pair_new_device_after_a_connect_attempt_restarts_the_module),
        TEST_CASE(a_device_chosen_during_a_restart_connects_once_the_module_is_ready),
        TEST_CASE(leaving_the_screen_during_a_restart_drops_the_search),
        TEST_CASE(turning_bluetooth_off_during_a_restart_keeps_it_off),
        TEST_CASE(connecting_saves_the_device_and_says_so),
        TEST_CASE(playing_with_nothing_plugged_in_streams),
        TEST_CASE(headphones_take_the_sound_back),
        TEST_CASE(the_buds_wait_for_the_next_play_after_headphones),
        TEST_CASE(the_buds_pause_and_resume_playback),
        TEST_CASE(the_buds_resume_a_pause_that_went_to_standby_at_its_place),
        TEST_CASE(the_buds_resume_a_player_pause_from_standby),
        TEST_CASE(reports_right_after_the_stream_starts_are_ignored),
        TEST_CASE(a_pause_report_right_after_play_is_ignored),
        TEST_CASE(a_pause_from_the_buds_says_so),
        TEST_CASE(a_settled_play_report_resumes_what_the_player_paused),
        TEST_CASE(the_buds_skip_tracks),
        TEST_CASE(presses_from_before_the_connection_are_ignored),
        TEST_CASE(losing_the_buds_pauses_playback),
        TEST_CASE(an_unused_module_switches_itself_off),
        TEST_CASE(browsing_leaves_a_dozing_module_off_until_asked),
        TEST_CASE(browsing_searches_for_devices),
        TEST_CASE(pairing_connects_the_chosen_device),
        TEST_CASE(a_search_that_finds_remembered_headphones_connects_them),
        TEST_CASE(an_old_search_result_does_not_interrupt_a_new_search),
        TEST_CASE(after_pair_new_device_remembered_headphones_only_show_as_nearby),
        TEST_CASE(devices_are_remembered_most_recent_first),
        TEST_CASE(choosing_the_connected_device_disconnects_and_stays_off),
        TEST_CASE(choosing_another_device_switches_to_it),
        TEST_CASE(searching_disconnects_and_scans_without_reconnecting),
        TEST_CASE(a_store_of_another_version_loads_nothing),
        TEST_CASE(forgetting_disconnects_and_clears_the_device),
        TEST_CASE(sleeping_an_enabled_module_is_idempotent_and_resume_wakes_it),
        TEST_CASE(choosing_a_different_device_during_paired_confirmation_resets_the_old_one),
        TEST_CASE(a_seventh_device_evicts_only_the_least_recent_saved_device),
        TEST_CASE(discovery_skips_unknown_devices_and_connects_the_known_match),
        TEST_CASE(a_newly_found_saved_device_can_connect_while_ready),
        TEST_CASE(forgetting_another_saved_device_keeps_the_chosen_target),
        TEST_CASE(invalid_saved_device_indices_leave_the_connection_and_store_alone),
        TEST_CASE(forgetting_a_pending_connection_cancels_it_without_retrying),
        TEST_CASE(forgetting_an_unconnected_device_keeps_the_other_connection),
        TEST_CASE(a_remembered_device_updates_its_name_when_it_reconnects),
        TEST_CASE(forgetting_the_first_device_preserves_and_retries_the_next_one),
        TEST_CASE(connecting_without_a_toast_still_remembers_the_device),
        TEST_CASE(play_after_bluetooth_switched_itself_off_reconnects_and_then_plays),
        TEST_CASE(play_right_after_power_on_waits_for_the_last_headphones),
        TEST_CASE(headphones_that_stay_away_leave_the_player_paused),
        TEST_CASE(play_during_the_wait_waits_again),
        TEST_CASE(wired_headphones_play_at_once_instead_of_waiting),
        TEST_CASE(plugging_in_while_waiting_plays_on_the_wired_headphones),
        TEST_CASE(music_last_played_on_the_jack_starts_without_waiting),
        TEST_CASE(the_output_music_last_used_is_remembered),
        TEST_CASE(headphones_disconnected_on_purpose_are_not_waited_for),
        TEST_CASE(headphones_back_in_range_reconnect_without_playing),
        TEST_CASE(a_wait_keeps_the_module_awake),
        TEST_CASE(a_short_or_out_of_range_store_loads_nothing),
        TEST_CASE(device_state_classifies_each_transient_condition),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
