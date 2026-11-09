#include "check.h"
#include "device/headset_remote.h"

enum {
    SAMPLE_MS = 20,
    IDLE_LEVEL = 800,
    PLAY_LEVEL = 5,
    VOLUME_UP_LEVEL = 30,
    VOLUME_DOWN_LEVEL = 70
};

typedef struct Rig {
    HeadsetRemote remote;
    uint32_t now_ms;
} Rig;

static void feed(Rig *rig, uint16_t level, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += SAMPLE_MS) {
        headset_remote_sample(&rig->remote, level, true, rig->now_ms);
        rig->now_ms += SAMPLE_MS;
    }
}

static Rig plugged_rig(void)
{
    Rig rig = {.now_ms = 1000};
    headset_remote_init(&rig.remote);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_SETTLE_MS + SAMPLE_MS);
    return rig;
}

static void click(Rig *rig, uint16_t level)
{
    feed(rig, level, 100);
    feed(rig, IDLE_LEVEL, 100);
}

static int drain(Rig *rig, HalHeadsetCommand *commands, int capacity)
{
    int count = 0;
    while (count < capacity && headset_remote_next(&rig->remote, &commands[count])) {
        count++;
    }
    return count;
}

static void levels_map_to_stock_bands(void)
{
    CHECK_EQUAL(HAL_HEADSET_BUTTON_PLAY, headset_remote_button(0));
    CHECK_EQUAL(HAL_HEADSET_BUTTON_PLAY, headset_remote_button(15));
    CHECK_EQUAL(HAL_HEADSET_BUTTON_VOLUME_UP, headset_remote_button(16));
    CHECK_EQUAL(HAL_HEADSET_BUTTON_VOLUME_UP, headset_remote_button(45));
    CHECK_EQUAL(HAL_HEADSET_BUTTON_VOLUME_DOWN, headset_remote_button(46));
    CHECK_EQUAL(HAL_HEADSET_BUTTON_VOLUME_DOWN, headset_remote_button(100));
    CHECK_EQUAL(HAL_HEADSET_BUTTON_NONE, headset_remote_button(101));
}

static void a_volume_tap_changes_the_volume_on_release(void)
{
    Rig rig = plugged_rig();
    feed(&rig, VOLUME_UP_LEVEL, 100);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
    feed(&rig, IDLE_LEVEL, 60);
    CHECK(headset_remote_next(&rig.remote, &command));
    CHECK_EQUAL(HAL_HEADSET_VOLUME_UP, command);
    click(&rig, VOLUME_DOWN_LEVEL);
    CHECK(headset_remote_next(&rig.remote, &command));
    CHECK_EQUAL(HAL_HEADSET_VOLUME_DOWN, command);
}

static void holding_volume_up_skips_to_the_next_track_once(void)
{
    Rig rig = plugged_rig();
    feed(&rig, VOLUME_UP_LEVEL, 2000);
    feed(&rig, IDLE_LEVEL, 100);
    HalHeadsetCommand commands[4];
    CHECK_EQUAL(1, drain(&rig, commands, 4));
    CHECK_EQUAL(HAL_HEADSET_NEXT, commands[0]);
}

static void holding_volume_down_goes_to_the_previous_track(void)
{
    Rig rig = plugged_rig();
    feed(&rig, VOLUME_DOWN_LEVEL, HEADSET_REMOTE_LONG_PRESS_MS + 100);
    HalHeadsetCommand command;
    CHECK(headset_remote_next(&rig.remote, &command));
    CHECK_EQUAL(HAL_HEADSET_PREVIOUS, command);
}

static void one_play_click_toggles_pause_after_the_click_gap(void)
{
    Rig rig = plugged_rig();
    click(&rig, PLAY_LEVEL);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    CHECK(headset_remote_next(&rig.remote, &command));
    CHECK_EQUAL(HAL_HEADSET_TOGGLE_PAUSE, command);
}

static void two_play_clicks_skip_to_the_next_track(void)
{
    Rig rig = plugged_rig();
    click(&rig, PLAY_LEVEL);
    click(&rig, PLAY_LEVEL);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand commands[4];
    CHECK_EQUAL(1, drain(&rig, commands, 4));
    CHECK_EQUAL(HAL_HEADSET_NEXT, commands[0]);
}

static void three_play_clicks_go_to_the_previous_track(void)
{
    Rig rig = plugged_rig();
    click(&rig, PLAY_LEVEL);
    click(&rig, PLAY_LEVEL);
    click(&rig, PLAY_LEVEL);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand commands[4];
    CHECK_EQUAL(1, drain(&rig, commands, 4));
    CHECK_EQUAL(HAL_HEADSET_PREVIOUS, commands[0]);
}

static void more_than_three_play_clicks_do_nothing(void)
{
    Rig rig = plugged_rig();
    for (int index = 0; index < 5; index++) {
        click(&rig, PLAY_LEVEL);
    }
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

static void a_held_play_button_does_nothing(void)
{
    Rig rig = plugged_rig();
    feed(&rig, PLAY_LEVEL, 3000);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS + 100);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

static void a_single_glitch_sample_is_ignored(void)
{
    Rig rig = plugged_rig();
    feed(&rig, PLAY_LEVEL, SAMPLE_MS);
    feed(&rig, IDLE_LEVEL, 1000);
    feed(&rig, VOLUME_UP_LEVEL, SAMPLE_MS);
    feed(&rig, IDLE_LEVEL, 1000);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

static void a_three_pole_plug_never_counts_as_a_click(void)
{
    Rig rig = {.now_ms = 1000};
    headset_remote_init(&rig.remote);
    feed(&rig, PLAY_LEVEL, 5000);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

static void feed_jitter(Rig *rig, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += SAMPLE_MS) {
        uint16_t level = (elapsed / SAMPLE_MS) % 3 == 0 ? 1 : 0;
        headset_remote_sample(&rig->remote, level, true, rig->now_ms);
        rig->now_ms += SAMPLE_MS;
    }
}

static void a_plug_reading_play_from_insertion_has_no_mic(void)
{
    Rig rig = {.now_ms = 1000};
    headset_remote_init(&rig.remote);
    feed(&rig, 0, HEADSET_REMOTE_SETTLE_MS + 100);
    CHECK(headset_remote_no_mic(&rig.remote));
    feed(&rig, IDLE_LEVEL, 100);
    click(&rig, PLAY_LEVEL);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

static void a_jittering_three_pole_plug_never_taps(void)
{
    Rig rig = {.now_ms = 1000};
    headset_remote_init(&rig.remote);
    feed_jitter(&rig, 10000);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
    CHECK(headset_remote_no_mic(&rig.remote));
}

static void unplugging_forgets_the_missing_mic(void)
{
    Rig rig = {.now_ms = 1000};
    headset_remote_init(&rig.remote);
    feed(&rig, 0, HEADSET_REMOTE_SETTLE_MS + 100);
    headset_remote_sample(&rig.remote, IDLE_LEVEL, false, rig.now_ms);
    CHECK(!headset_remote_no_mic(&rig.remote));
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_SETTLE_MS + SAMPLE_MS);
    click(&rig, PLAY_LEVEL);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand command;
    CHECK(headset_remote_next(&rig.remote, &command));
    CHECK_EQUAL(HAL_HEADSET_TOGGLE_PAUSE, command);
}

static void a_long_play_press_on_a_remote_keeps_the_remote(void)
{
    Rig rig = plugged_rig();
    feed(&rig, PLAY_LEVEL, 10000);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_SETTLE_MS + 100);
    CHECK(!headset_remote_no_mic(&rig.remote));
    click(&rig, PLAY_LEVEL);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand commands[4];
    CHECK_EQUAL(1, drain(&rig, commands, 4));
    CHECK_EQUAL(HAL_HEADSET_TOGGLE_PAUSE, commands[0]);
}

static void a_swap_to_a_three_pole_plug_without_a_seen_unplug_mutes_the_remote(void)
{
    Rig rig = plugged_rig();
    click(&rig, VOLUME_UP_LEVEL);
    HalHeadsetCommand commands[4];
    CHECK_EQUAL(1, drain(&rig, commands, 4));
    feed_jitter(&rig, 60000);
    CHECK(headset_remote_no_mic(&rig.remote));
    CHECK_EQUAL(0, drain(&rig, commands, 4));
}

static void presses_while_unplugged_are_ignored(void)
{
    Rig rig = plugged_rig();
    for (int index = 0; index < 10; index++) {
        headset_remote_sample(&rig.remote, PLAY_LEVEL, false, rig.now_ms);
        rig.now_ms += SAMPLE_MS;
    }
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_SETTLE_MS + HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

static void a_press_while_the_plug_settles_is_ignored(void)
{
    Rig rig = {.now_ms = 1000};
    headset_remote_init(&rig.remote);
    click(&rig, PLAY_LEVEL);
    feed(&rig, IDLE_LEVEL, HEADSET_REMOTE_SETTLE_MS + HEADSET_REMOTE_CLICK_GAP_MS);
    HalHeadsetCommand command;
    CHECK(!headset_remote_next(&rig.remote, &command));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(levels_map_to_stock_bands),
        TEST_CASE(a_volume_tap_changes_the_volume_on_release),
        TEST_CASE(holding_volume_up_skips_to_the_next_track_once),
        TEST_CASE(holding_volume_down_goes_to_the_previous_track),
        TEST_CASE(one_play_click_toggles_pause_after_the_click_gap),
        TEST_CASE(two_play_clicks_skip_to_the_next_track),
        TEST_CASE(three_play_clicks_go_to_the_previous_track),
        TEST_CASE(more_than_three_play_clicks_do_nothing),
        TEST_CASE(a_held_play_button_does_nothing),
        TEST_CASE(a_single_glitch_sample_is_ignored),
        TEST_CASE(a_three_pole_plug_never_counts_as_a_click),
        TEST_CASE(a_plug_reading_play_from_insertion_has_no_mic),
        TEST_CASE(a_jittering_three_pole_plug_never_taps),
        TEST_CASE(unplugging_forgets_the_missing_mic),
        TEST_CASE(a_long_play_press_on_a_remote_keeps_the_remote),
        TEST_CASE(a_swap_to_a_three_pole_plug_without_a_seen_unplug_mutes_the_remote),
        TEST_CASE(presses_while_unplugged_are_ignored),
        TEST_CASE(a_press_while_the_plug_settles_is_ignored),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
