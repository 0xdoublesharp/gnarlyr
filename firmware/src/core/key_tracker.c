#include "core/key_tracker.h"

#include "core/deadline.h"

static bool repeats(const KeyTracker *tracker, Key key)
{
    return tracker->config.behaviors[key] == KEY_BEHAVIOR_REPEAT;
}

static uint32_t supported_delay(uint32_t delay_ms)
{
    return delay_ms <= INT32_MAX ? delay_ms : INT32_MAX;
}

static bool repeats_after_hold(const KeyTracker *tracker, Key key)
{
    return tracker->config.behaviors[key] == KEY_BEHAVIOR_TAP_HOLD_REPEAT;
}

static uint8_t click_limit(const KeyTracker *tracker, Key key)
{
    switch (tracker->config.behaviors[key]) {
    case KEY_BEHAVIOR_DOUBLE_CLICK_REPEAT:
        return 2;
    case KEY_BEHAVIOR_TRIPLE_CLICK_OR_HOLD:
        return 3;
    case KEY_BEHAVIOR_TAP_OR_HOLD:
    case KEY_BEHAVIOR_REPEAT:
    case KEY_BEHAVIOR_TAP_HOLD_REPEAT:
        return 0;
    }
    return 0;
}

static uint32_t long_press_ms(const KeyTracker *tracker, Key key)
{
    uint32_t own = tracker->config.long_press_ms[key];
    return own != 0 ? own : tracker->config.timing.long_press_ms;
}

static bool long_press_due(const KeyTracker *tracker, Key key, uint32_t now_ms)
{
    return deadline_reached(now_ms,
                            tracker->states[key].pressed_at_ms + long_press_ms(tracker, key));
}

static void push_event(KeyTracker *tracker, Key key, KeyGesture gesture)
{
    if (tracker->queue_length == KEY_TRACKER_QUEUE_CAPACITY) {
        return;
    }
    uint32_t slot = (tracker->queue_start + tracker->queue_length) % KEY_TRACKER_QUEUE_CAPACITY;
    tracker->queue[slot] =
        (KeyEvent){.key = key, .gesture = gesture, .repeats = tracker->states[key].repeats};
    tracker->queue_length++;
}

void key_tracker_init(KeyTracker *tracker, const KeyConfig *config)
{
    *tracker = (KeyTracker){.config = *config};
    tracker->config.timing.long_press_ms = supported_delay(config->timing.long_press_ms);
    tracker->config.timing.repeat_delay_ms = supported_delay(config->timing.repeat_delay_ms);
    tracker->config.timing.repeat_interval_ms = supported_delay(config->timing.repeat_interval_ms);
    tracker->config.timing.click_window_ms = supported_delay(config->timing.click_window_ms);
    for (int key = 0; key < KEY_COUNT; key++) {
        tracker->config.long_press_ms[key] = supported_delay(config->long_press_ms[key]);
    }
}

void key_tracker_set_behavior(KeyTracker *tracker, Key key, KeyBehavior behavior)
{
    tracker->config.behaviors[key] = behavior;
}

void key_tracker_key_down(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    if (state->held) {
        return;
    }
    *state = (KeyState){
        .held = true,
        .pressed_at_ms = now_ms,
        .next_repeat_at_ms = now_ms + tracker->config.timing.repeat_delay_ms,
        .clicks = state->clicks,
        .clicked_at_ms = state->clicked_at_ms,
    };
    if (repeats(tracker, key)) {
        push_event(tracker, key, KEY_GESTURE_PRESS);
    }
}

static void flush_clicks(KeyTracker *tracker, Key key)
{
    static const KeyGesture BY_CLICKS[] = {KEY_GESTURE_PRESS, KEY_GESTURE_DOUBLE_PRESS,
                                           KEY_GESTURE_TRIPLE_PRESS};
    KeyState *state = &tracker->states[key];
    push_event(tracker, key, BY_CLICKS[state->clicks - 1]);
    state->clicks = 0;
}

static void count_click(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    state->clicks++;
    state->clicked_at_ms = now_ms;
    if (state->clicks >= click_limit(tracker, key)) {
        flush_clicks(tracker, key);
    }
}

static void finish_tap_or_hold(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    if (tracker->states[key].long_press_sent) {
        return;
    }
    bool held_long = long_press_due(tracker, key, now_ms);
    if (held_long) {
        tracker->states[key].clicks = 0;
        push_event(tracker, key, KEY_GESTURE_LONG_PRESS);
    } else if (click_limit(tracker, key) > 0) {
        count_click(tracker, key, now_ms);
    } else {
        push_event(tracker, key, KEY_GESTURE_PRESS);
    }
}

static bool clicks_then_repeats(const KeyTracker *tracker, Key key)
{
    return tracker->config.behaviors[key] == KEY_BEHAVIOR_DOUBLE_CLICK_REPEAT;
}

void key_tracker_key_up(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    if (!tracker->states[key].held) {
        return;
    }
    if (clicks_then_repeats(tracker, key) && !tracker->states[key].long_press_sent) {
        count_click(tracker, key, now_ms);
    } else if (!repeats(tracker, key) && !clicks_then_repeats(tracker, key)) {
        finish_tap_or_hold(tracker, key, now_ms);
    }
    tracker->states[key].held = false;
}

void key_tracker_cancel(KeyTracker *tracker, Key key)
{
    tracker->states[key] = (KeyState){0};
}

static void update_repeat(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    if (!deadline_reached(now_ms, state->next_repeat_at_ms)) {
        return;
    }
    push_event(tracker, key, KEY_GESTURE_REPEAT);
    state->next_repeat_at_ms = now_ms + tracker->config.timing.repeat_interval_ms;
}

static void update_long_press(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    if (state->long_press_sent || !long_press_due(tracker, key, now_ms)) {
        return;
    }
    push_event(tracker, key, KEY_GESTURE_LONG_PRESS);
    state->long_press_sent = true;
    state->clicks = 0;
    state->next_repeat_at_ms = now_ms + tracker->config.timing.repeat_interval_ms;
}

static void update_click_hold(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    if (!deadline_reached(now_ms, state->next_repeat_at_ms)) {
        return;
    }
    bool started = state->long_press_sent;
    state->long_press_sent = true;
    state->clicks = 0;
    push_event(tracker, key, started ? KEY_GESTURE_REPEAT : KEY_GESTURE_LONG_PRESS);
    if (state->repeats < UINT8_MAX) {
        state->repeats++;
    }
    state->next_repeat_at_ms = now_ms + tracker->config.timing.repeat_interval_ms;
}

static void update_held(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    if (clicks_then_repeats(tracker, key)) {
        update_click_hold(tracker, key, now_ms);
        return;
    }
    bool repeating = repeats(tracker, key) ||
                     (repeats_after_hold(tracker, key) && tracker->states[key].long_press_sent);
    if (repeating) {
        update_repeat(tracker, key, now_ms);
    } else {
        update_long_press(tracker, key, now_ms);
    }
}

static bool click_window_over(const KeyTracker *tracker, Key key, uint32_t now_ms)
{
    const KeyState *state = &tracker->states[key];
    return state->clicks > 0 &&
           deadline_reached(now_ms, state->clicked_at_ms + tracker->config.timing.click_window_ms);
}

void key_tracker_update(KeyTracker *tracker, uint32_t now_ms)
{
    for (int index = 0; index < KEY_COUNT; index++) {
        Key key = (Key)index;
        if (tracker->states[key].held) {
            update_held(tracker, key, now_ms);
        } else if (click_window_over(tracker, key, now_ms)) {
            flush_clicks(tracker, key);
        }
    }
}

bool key_tracker_next_event(KeyTracker *tracker, KeyEvent *event)
{
    if (tracker->queue_length == 0) {
        return false;
    }
    *event = tracker->queue[tracker->queue_start];
    tracker->queue_start = (tracker->queue_start + 1) % KEY_TRACKER_QUEUE_CAPACITY;
    tracker->queue_length--;
    return true;
}
