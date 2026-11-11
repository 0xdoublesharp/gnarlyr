#include "check.h"
#include "core/key_tracker.h"

enum {
    LONG_PRESS_MS = 600,
    REPEAT_DELAY_MS = 400,
    REPEAT_INTERVAL_MS = 100,
    CLICK_WINDOW_MS = 300,
    NO_EVENT = -1
};

static KeyTracker make_tracker(void)
{
    KeyConfig config = {
        .long_press_ms = {[KEY_POWER] = 2000},
        .timing = {LONG_PRESS_MS, REPEAT_DELAY_MS, REPEAT_INTERVAL_MS, CLICK_WINDOW_MS},
        .behaviors =
            {
                [KEY_VOLUME_UP] = KEY_BEHAVIOR_REPEAT,
                [KEY_VOLUME_DOWN] = KEY_BEHAVIOR_REPEAT,
                [KEY_PLAY] = KEY_BEHAVIOR_TAP_OR_HOLD,
                [KEY_MENU] = KEY_BEHAVIOR_TAP_OR_HOLD,
                [KEY_BACK] = KEY_BEHAVIOR_TAP_OR_HOLD,
                [KEY_POWER] = KEY_BEHAVIOR_TAP_OR_HOLD,
            },
    };
    KeyTracker tracker;
    key_tracker_init(&tracker, &config);
    return tracker;
}

static int next_gesture(KeyTracker *tracker, Key expected_key)
{
    KeyEvent event;
    if (!key_tracker_next_event(tracker, &event)) {
        return NO_EVENT;
    }
    CHECK_EQUAL(expected_key, event.key);
    return (int)event.gesture;
}

static void tap_emits_press_on_release(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_PLAY, 1000);
    key_tracker_update(&tracker, 1100);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_PLAY));
    key_tracker_key_up(&tracker, KEY_PLAY, 1150);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_PLAY));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_PLAY));
}

static void holding_past_threshold_emits_one_long_press(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_MENU, 1000);
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS);
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS + 500);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_MENU));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_MENU));
}

static void release_after_long_press_emits_nothing(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_BACK, 1000);
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS);
    key_tracker_key_up(&tracker, KEY_BACK, 1000 + LONG_PRESS_MS + 50);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_BACK));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_BACK));
}

static void late_release_without_update_emits_long_press(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_PLAY, 1000);
    key_tracker_key_up(&tracker, KEY_PLAY, 1000 + LONG_PRESS_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_PLAY));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_PLAY));
}

static void repeat_key_emits_press_on_down(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_key_up(&tracker, KEY_VOLUME_UP, 1050);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void repeat_key_repeats_after_delay_then_every_interval(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_DOWN, 1000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_DOWN));
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_DOWN));
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS);
    CHECK_EQUAL(KEY_GESTURE_REPEAT, next_gesture(&tracker, KEY_VOLUME_DOWN));
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS + REPEAT_INTERVAL_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_DOWN));
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS + REPEAT_INTERVAL_MS);
    CHECK_EQUAL(KEY_GESTURE_REPEAT, next_gesture(&tracker, KEY_VOLUME_DOWN));
}

static void releasing_a_repeat_key_stops_repeats(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_key_up(&tracker, KEY_VOLUME_UP, 1100);
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS * 3);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void a_second_down_while_held_is_ignored(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1010);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void a_release_without_press_is_ignored(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_up(&tracker, KEY_PLAY, 1000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_PLAY));
}

static void keys_are_tracked_independently(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_PLAY, 1000);
    key_tracker_key_down(&tracker, KEY_BACK, 1100);
    key_tracker_key_up(&tracker, KEY_BACK, 1200);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_BACK));
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_PLAY));
}

static void a_full_queue_keeps_the_oldest_events(void)
{
    KeyTracker tracker = make_tracker();
    for (uint32_t index = 0; index < KEY_TRACKER_QUEUE_CAPACITY + 3; index++) {
        uint32_t now_ms = 1000 + index * 10;
        Key key = index < KEY_TRACKER_QUEUE_CAPACITY ? KEY_PLAY : KEY_MENU;
        key_tracker_key_down(&tracker, key, now_ms);
        key_tracker_key_up(&tracker, key, now_ms + 1);
    }
    for (uint32_t index = 0; index < KEY_TRACKER_QUEUE_CAPACITY; index++) {
        CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_PLAY));
    }
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_PLAY));
}

static void repeat_timing_survives_clock_wraparound(void)
{
    KeyTracker tracker = make_tracker();
    uint32_t pressed_at_ms = UINT32_MAX - 100;
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, pressed_at_ms);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, pressed_at_ms + 50);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, pressed_at_ms + REPEAT_DELAY_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, pressed_at_ms + REPEAT_DELAY_MS);
    CHECK_EQUAL(KEY_GESTURE_REPEAT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void long_press_timing_survives_clock_wraparound(void)
{
    KeyTracker tracker = make_tracker();
    uint32_t pressed_at_ms = UINT32_MAX - 100;
    key_tracker_key_down(&tracker, KEY_MENU, pressed_at_ms);
    key_tracker_update(&tracker, pressed_at_ms + 50);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_MENU));
    key_tracker_update(&tracker, pressed_at_ms + LONG_PRESS_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_MENU));
    key_tracker_update(&tracker, pressed_at_ms + LONG_PRESS_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_MENU));
}

static void a_press_stamped_after_the_update_time_is_not_a_long_press(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_POWER, 1005);
    key_tracker_update(&tracker, 1000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_POWER));
    key_tracker_key_up(&tracker, KEY_POWER, 1100);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_POWER));
}

static KeyTracker hold_repeat_tracker(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_set_behavior(&tracker, KEY_VOLUME_UP, KEY_BEHAVIOR_TAP_HOLD_REPEAT);
    return tracker;
}

static void a_hold_repeat_key_taps_on_release(void)
{
    KeyTracker tracker = hold_repeat_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_key_up(&tracker, KEY_VOLUME_UP, 1000 + REPEAT_DELAY_MS + 10);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void a_held_hold_repeat_key_long_presses_then_repeats(void)
{
    KeyTracker tracker = hold_repeat_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS + REPEAT_INTERVAL_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS + REPEAT_INTERVAL_MS);
    CHECK_EQUAL(KEY_GESTURE_REPEAT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_key_up(&tracker, KEY_VOLUME_UP, 1000 + LONG_PRESS_MS + REPEAT_INTERVAL_MS + 10);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void changing_a_behavior_leaves_other_keys_alone(void)
{
    KeyTracker tracker = hold_repeat_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_DOWN, 1000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_DOWN));
}

static void a_key_with_its_own_long_press_time_waits_for_it(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_POWER, 1000);
    key_tracker_update(&tracker, 2500);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_POWER));
    key_tracker_update(&tracker, 3000);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_POWER));
}

static void a_release_before_its_own_long_press_time_is_a_tap(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_POWER, 1000);
    key_tracker_update(&tracker, 2000);
    key_tracker_key_up(&tracker, KEY_POWER, 2500);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_POWER));
}

static void keys_without_their_own_time_use_the_shared_one(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_key_down(&tracker, KEY_PLAY, 1000);
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_PLAY));
}

static KeyTracker click_tracker(void)
{
    KeyTracker tracker = make_tracker();
    key_tracker_set_behavior(&tracker, KEY_VOLUME_UP, KEY_BEHAVIOR_DOUBLE_CLICK_REPEAT);
    key_tracker_set_behavior(&tracker, KEY_PLAY, KEY_BEHAVIOR_TRIPLE_CLICK_OR_HOLD);
    return tracker;
}

static void click(KeyTracker *tracker, Key key, uint32_t at_ms)
{
    key_tracker_key_down(tracker, key, at_ms);
    key_tracker_key_up(tracker, key, at_ms + 30);
}

static void a_single_click_is_delivered_after_the_click_window(void)
{
    KeyTracker tracker = click_tracker();
    click(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_update(&tracker, 1030 + CLICK_WINDOW_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1030 + CLICK_WINDOW_MS);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void two_clicks_inside_the_window_make_a_double_press_at_once(void)
{
    KeyTracker tracker = click_tracker();
    click(&tracker, KEY_VOLUME_UP, 1000);
    click(&tracker, KEY_VOLUME_UP, 1150);
    CHECK_EQUAL(KEY_GESTURE_DOUBLE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 3000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void clicks_further_apart_than_the_window_are_two_single_presses(void)
{
    KeyTracker tracker = click_tracker();
    click(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_update(&tracker, 1500);
    click(&tracker, KEY_VOLUME_UP, 1600);
    key_tracker_update(&tracker, 2000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void holding_a_click_repeat_key_long_presses_then_repeats_counting_them(void)
{
    KeyTracker tracker = click_tracker();
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS - 1);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1000 + REPEAT_DELAY_MS + REPEAT_INTERVAL_MS);
    KeyEvent repeat;
    CHECK(key_tracker_next_event(&tracker, &repeat));
    CHECK_EQUAL(KEY_GESTURE_REPEAT, repeat.gesture);
    CHECK_EQUAL(1, repeat.repeats);
    key_tracker_update(&tracker, 1000 + LONG_PRESS_MS + 1000);
    CHECK_EQUAL(KEY_GESTURE_REPEAT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_key_up(&tracker, KEY_VOLUME_UP, 4000);
    key_tracker_update(&tracker, 5000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void a_triple_click_key_counts_up_to_three(void)
{
    KeyTracker tracker = click_tracker();
    click(&tracker, KEY_PLAY, 1000);
    click(&tracker, KEY_PLAY, 1100);
    click(&tracker, KEY_PLAY, 1200);
    CHECK_EQUAL(KEY_GESTURE_TRIPLE_PRESS, next_gesture(&tracker, KEY_PLAY));
    click(&tracker, KEY_PLAY, 2000);
    click(&tracker, KEY_PLAY, 2100);
    key_tracker_update(&tracker, 2130 + CLICK_WINDOW_MS);
    CHECK_EQUAL(KEY_GESTURE_DOUBLE_PRESS, next_gesture(&tracker, KEY_PLAY));
    click(&tracker, KEY_PLAY, 3000);
    key_tracker_update(&tracker, 3030 + CLICK_WINDOW_MS);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_PLAY));
}

static void a_triple_click_key_still_long_presses(void)
{
    KeyTracker tracker = click_tracker();
    click(&tracker, KEY_PLAY, 1000);
    key_tracker_key_down(&tracker, KEY_PLAY, 1100);
    key_tracker_update(&tracker, 1100 + LONG_PRESS_MS);
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_PLAY));
    key_tracker_key_up(&tracker, KEY_PLAY, 2500);
    key_tracker_update(&tracker, 4000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_PLAY));
}

static void click_counting_survives_clock_wraparound(void)
{
    KeyTracker tracker = click_tracker();
    uint32_t start = UINT32_MAX - 100;
    click(&tracker, KEY_VOLUME_UP, start);
    key_tracker_update(&tracker, start + 30 + CLICK_WINDOW_MS);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
}

static KeyTracker tracker_with_timing(KeyTiming timing)
{
    KeyTracker original = make_tracker();
    KeyConfig config = original.config;
    config.timing = timing;
    KeyTracker tracker;
    key_tracker_init(&tracker, &config);
    return tracker;
}

static void maximum_shared_long_press_delay_is_not_immediate(void)
{
    KeyTracker tracker = tracker_with_timing(
        (KeyTiming){UINT32_MAX, REPEAT_DELAY_MS, REPEAT_INTERVAL_MS, CLICK_WINDOW_MS});
    key_tracker_key_down(&tracker, KEY_MENU, 1000);
    key_tracker_update(&tracker, 1000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_MENU));
    key_tracker_update(&tracker, 1000 + UINT32_C(0x7fffffff));
    CHECK_EQUAL(KEY_GESTURE_LONG_PRESS, next_gesture(&tracker, KEY_MENU));
}

static void maximum_per_key_long_press_delay_is_not_immediate(void)
{
    KeyTracker original = make_tracker();
    KeyConfig config = original.config;
    config.long_press_ms[KEY_POWER] = UINT32_MAX;
    KeyTracker tracker;
    key_tracker_init(&tracker, &config);
    key_tracker_key_down(&tracker, KEY_POWER, 1000);
    key_tracker_update(&tracker, 1000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_POWER));
}

static void maximum_repeat_delay_is_not_immediate(void)
{
    KeyTracker tracker = tracker_with_timing(
        (KeyTiming){LONG_PRESS_MS, UINT32_MAX, REPEAT_INTERVAL_MS, CLICK_WINDOW_MS});
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1000);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void maximum_repeat_interval_is_not_immediate(void)
{
    KeyTracker tracker =
        tracker_with_timing((KeyTiming){LONG_PRESS_MS, 1, UINT32_MAX, CLICK_WINDOW_MS});
    key_tracker_key_down(&tracker, KEY_VOLUME_UP, 1000);
    CHECK_EQUAL(KEY_GESTURE_PRESS, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1001);
    CHECK_EQUAL(KEY_GESTURE_REPEAT, next_gesture(&tracker, KEY_VOLUME_UP));
    key_tracker_update(&tracker, 1001);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

static void maximum_click_window_is_not_immediate(void)
{
    KeyTracker tracker = tracker_with_timing(
        (KeyTiming){LONG_PRESS_MS, REPEAT_DELAY_MS, REPEAT_INTERVAL_MS, UINT32_MAX});
    key_tracker_set_behavior(&tracker, KEY_VOLUME_UP, KEY_BEHAVIOR_DOUBLE_CLICK_REPEAT);
    click(&tracker, KEY_VOLUME_UP, 1000);
    key_tracker_update(&tracker, 1030);
    CHECK_EQUAL(NO_EVENT, next_gesture(&tracker, KEY_VOLUME_UP));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_key_with_its_own_long_press_time_waits_for_it),
        TEST_CASE(a_release_before_its_own_long_press_time_is_a_tap),
        TEST_CASE(keys_without_their_own_time_use_the_shared_one),
        TEST_CASE(tap_emits_press_on_release),
        TEST_CASE(holding_past_threshold_emits_one_long_press),
        TEST_CASE(release_after_long_press_emits_nothing),
        TEST_CASE(late_release_without_update_emits_long_press),
        TEST_CASE(repeat_key_emits_press_on_down),
        TEST_CASE(repeat_key_repeats_after_delay_then_every_interval),
        TEST_CASE(releasing_a_repeat_key_stops_repeats),
        TEST_CASE(a_second_down_while_held_is_ignored),
        TEST_CASE(a_release_without_press_is_ignored),
        TEST_CASE(keys_are_tracked_independently),
        TEST_CASE(a_full_queue_keeps_the_oldest_events),
        TEST_CASE(repeat_timing_survives_clock_wraparound),
        TEST_CASE(long_press_timing_survives_clock_wraparound),
        TEST_CASE(a_press_stamped_after_the_update_time_is_not_a_long_press),
        TEST_CASE(a_hold_repeat_key_taps_on_release),
        TEST_CASE(a_held_hold_repeat_key_long_presses_then_repeats),
        TEST_CASE(changing_a_behavior_leaves_other_keys_alone),
        TEST_CASE(a_single_click_is_delivered_after_the_click_window),
        TEST_CASE(two_clicks_inside_the_window_make_a_double_press_at_once),
        TEST_CASE(clicks_further_apart_than_the_window_are_two_single_presses),
        TEST_CASE(holding_a_click_repeat_key_long_presses_then_repeats_counting_them),
        TEST_CASE(a_triple_click_key_counts_up_to_three),
        TEST_CASE(a_triple_click_key_still_long_presses),
        TEST_CASE(click_counting_survives_clock_wraparound),
        TEST_CASE(maximum_shared_long_press_delay_is_not_immediate),
        TEST_CASE(maximum_per_key_long_press_delay_is_not_immediate),
        TEST_CASE(maximum_repeat_delay_is_not_immediate),
        TEST_CASE(maximum_repeat_interval_is_not_immediate),
        TEST_CASE(maximum_click_window_is_not_immediate),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
