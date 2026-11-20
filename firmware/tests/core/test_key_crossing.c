#include "check.h"
#include "core/key_crossing.h"

static const KeyCrossingRules WATCH_MENU = {.menu = true};
static const KeyCrossingRules WATCH_BOTH = {.menu = true, .volume = true};

static KeyCrossing crossing;
static KeyCrossingOut out;

static void take(Key key, bool pressed, uint32_t time_ms, KeyCrossingRules rules)
{
    out = (KeyCrossingOut){0};
    key_crossing_take(&crossing, (KeyStep){key, pressed, time_ms}, rules, &out);
}

static void settle(uint32_t now_ms)
{
    out = (KeyCrossingOut){0};
    key_crossing_settle(&crossing, now_ms, &out);
}

static bool step_is(int index, Key key, bool pressed, uint32_t time_ms)
{
    return out.steps[index].key == key && out.steps[index].pressed == pressed &&
           out.steps[index].time_ms == time_ms;
}

static void start(void)
{
    key_crossing_reset(&crossing);
}

static void a_menu_release_alone_passes_at_the_end_of_the_batch(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    CHECK_EQUAL(0, out.count);
    settle(100);
    CHECK_EQUAL(1, out.count);
    CHECK(step_is(0, KEY_MENU, false, 100));
}

static void menu_straight_to_volume_passes_unchanged(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    take(KEY_VOLUME_UP, true, 100, WATCH_MENU);
    CHECK_EQUAL(2, out.count);
    CHECK(step_is(0, KEY_MENU, false, 100));
    CHECK(step_is(1, KEY_VOLUME_UP, true, 100));
}

static void menu_to_volume_through_the_play_band_reads_as_menu_to_volume(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    take(KEY_PLAY, true, 100, WATCH_MENU);
    settle(100);
    CHECK_EQUAL(0, out.count);
    settle(140);
    CHECK_EQUAL(0, out.count);
    take(KEY_PLAY, false, 150, WATCH_MENU);
    CHECK_EQUAL(0, out.count);
    take(KEY_VOLUME_DOWN, true, 150, WATCH_MENU);
    CHECK_EQUAL(2, out.count);
    CHECK(step_is(0, KEY_MENU, false, 100));
    CHECK(step_is(1, KEY_VOLUME_DOWN, true, 100));
}

static void volume_back_to_menu_through_the_play_band_reads_as_volume_to_menu(void)
{
    start();
    take(KEY_VOLUME_UP, false, 500, WATCH_BOTH);
    take(KEY_PLAY, true, 500, WATCH_BOTH);
    take(KEY_PLAY, false, 550, WATCH_BOTH);
    take(KEY_MENU, true, 550, WATCH_BOTH);
    CHECK_EQUAL(2, out.count);
    CHECK(step_is(0, KEY_VOLUME_UP, false, 500));
    CHECK(step_is(1, KEY_MENU, true, 500));
}

static void volume_through_the_play_band_to_idle_drops_the_play_blip(void)
{
    start();
    take(KEY_VOLUME_UP, false, 500, WATCH_BOTH);
    take(KEY_PLAY, true, 500, WATCH_BOTH);
    take(KEY_PLAY, false, 540, WATCH_BOTH);
    settle(540);
    CHECK_EQUAL(1, out.count);
    CHECK(step_is(0, KEY_VOLUME_UP, false, 500));
}

static void a_volume_release_is_not_watched_outside_a_menu_chord(void)
{
    start();
    take(KEY_VOLUME_UP, false, 500, WATCH_MENU);
    CHECK_EQUAL(1, out.count);
    take(KEY_PLAY, true, 500, WATCH_MENU);
    CHECK_EQUAL(1, out.count);
    CHECK(step_is(0, KEY_PLAY, true, 500));
}

static void play_held_past_the_window_under_menu_is_a_real_play_press(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    take(KEY_PLAY, true, 100, WATCH_MENU);
    settle(100 + KEY_CROSSING_WINDOW_MS - 1);
    CHECK_EQUAL(0, out.count);
    settle(100 + KEY_CROSSING_WINDOW_MS);
    CHECK_EQUAL(2, out.count);
    CHECK(step_is(0, KEY_MENU, false, 100));
    CHECK(step_is(1, KEY_PLAY, true, 100));
    take(KEY_PLAY, false, 400, WATCH_MENU);
    CHECK_EQUAL(1, out.count);
}

static void a_short_play_press_under_menu_still_counts_as_a_tap(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    take(KEY_PLAY, true, 100, WATCH_MENU);
    take(KEY_PLAY, false, 160, WATCH_MENU);
    take(KEY_MENU, true, 160, WATCH_MENU);
    CHECK_EQUAL(4, out.count);
    CHECK(step_is(0, KEY_MENU, false, 100));
    CHECK(step_is(1, KEY_PLAY, true, 100));
    CHECK(step_is(2, KEY_PLAY, false, 160));
    CHECK(step_is(3, KEY_MENU, true, 160));
}

static void a_volume_pressed_again_through_the_play_band_never_let_go(void)
{
    start();
    take(KEY_VOLUME_UP, false, 500, WATCH_BOTH);
    take(KEY_PLAY, true, 500, WATCH_BOTH);
    take(KEY_PLAY, false, 530, WATCH_BOTH);
    take(KEY_VOLUME_UP, true, 530, WATCH_BOTH);
    CHECK_EQUAL(0, out.count);
    settle(530);
    CHECK_EQUAL(0, out.count);
}

static void another_key_during_the_crossing_replays_what_came_before(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    take(KEY_PLAY, true, 100, WATCH_MENU);
    take(KEY_POWER, true, 120, WATCH_MENU);
    CHECK_EQUAL(3, out.count);
    CHECK(step_is(0, KEY_MENU, false, 100));
    CHECK(step_is(1, KEY_PLAY, true, 100));
    CHECK(step_is(2, KEY_POWER, true, 120));
}

static void nothing_is_watched_when_the_rules_are_off(void)
{
    start();
    take(KEY_MENU, false, 100, (KeyCrossingRules){0});
    CHECK_EQUAL(1, out.count);
}

static void a_reset_forgets_a_held_back_release(void)
{
    start();
    take(KEY_MENU, false, 100, WATCH_MENU);
    key_crossing_reset(&crossing);
    settle(100);
    CHECK_EQUAL(0, out.count);
}

static void a_crossing_remains_ordered_across_the_clock_wrap(void)
{
    start();
    uint32_t before_wrap = UINT32_MAX - 50;
    take(KEY_MENU, false, before_wrap, WATCH_MENU);
    take(KEY_PLAY, true, before_wrap, WATCH_MENU);
    take(KEY_PLAY, false, 25, WATCH_MENU);
    take(KEY_VOLUME_UP, true, 25, WATCH_MENU);
    CHECK_EQUAL(2, out.count);
    CHECK(step_is(0, KEY_MENU, false, before_wrap));
    CHECK(step_is(1, KEY_VOLUME_UP, true, before_wrap));
}

static void changing_between_volume_keys_drops_the_play_band_blip(void)
{
    start();
    take(KEY_VOLUME_UP, false, 100, WATCH_BOTH);
    take(KEY_PLAY, true, 100, WATCH_BOTH);
    take(KEY_PLAY, false, 150, WATCH_BOTH);
    take(KEY_VOLUME_DOWN, true, 150, WATCH_BOTH);
    CHECK_EQUAL(2, out.count);
    CHECK(step_is(0, KEY_VOLUME_UP, false, 100));
    CHECK(step_is(1, KEY_VOLUME_DOWN, true, 150));
}

static void a_full_output_batch_drops_extra_steps_without_overflowing(void)
{
    start();
    out = (KeyCrossingOut){.count = KEY_CROSSING_OUT_CAPACITY};
    key_crossing_take(&crossing, (KeyStep){KEY_POWER, true, 100}, WATCH_BOTH, &out);
    CHECK_EQUAL(KEY_CROSSING_OUT_CAPACITY, out.count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_menu_release_alone_passes_at_the_end_of_the_batch),
        TEST_CASE(menu_straight_to_volume_passes_unchanged),
        TEST_CASE(menu_to_volume_through_the_play_band_reads_as_menu_to_volume),
        TEST_CASE(volume_back_to_menu_through_the_play_band_reads_as_volume_to_menu),
        TEST_CASE(volume_through_the_play_band_to_idle_drops_the_play_blip),
        TEST_CASE(a_volume_release_is_not_watched_outside_a_menu_chord),
        TEST_CASE(play_held_past_the_window_under_menu_is_a_real_play_press),
        TEST_CASE(a_short_play_press_under_menu_still_counts_as_a_tap),
        TEST_CASE(a_volume_pressed_again_through_the_play_band_never_let_go),
        TEST_CASE(another_key_during_the_crossing_replays_what_came_before),
        TEST_CASE(nothing_is_watched_when_the_rules_are_off),
        TEST_CASE(a_reset_forgets_a_held_back_release),
        TEST_CASE(a_crossing_remains_ordered_across_the_clock_wrap),
        TEST_CASE(changing_between_volume_keys_drops_the_play_band_blip),
        TEST_CASE(a_full_output_batch_drops_extra_steps_without_overflowing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
