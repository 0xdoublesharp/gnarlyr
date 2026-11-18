#include "core/key_tracker.h"

#include "core/deadline.h"

static KeyBehavior behavior_of(const KeyTracker *tracker, Key key)
{
    return (KeyBehavior)tracker->states[key].behavior;
}

static bool repeats(const KeyTracker *tracker, Key key)
{
    return behavior_of(tracker, key) == KEY_BEHAVIOR_REPEAT;
}

static uint32_t supported_delay(uint32_t delay_ms)
{
    return delay_ms <= INT32_MAX ? delay_ms : INT32_MAX;
}

static bool repeats_after_hold(const KeyTracker *tracker, Key key)
{
    return behavior_of(tracker, key) == KEY_BEHAVIOR_TAP_HOLD_REPEAT;
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
    if (gesture == KEY_GESTURE_REPEAT && repeats(tracker, key) && tracker->queue_length > 0) {
        uint32_t last =
            (tracker->queue_start + tracker->queue_length - 1) % KEY_TRACKER_QUEUE_CAPACITY;
        if (tracker->queue[last].key == key && tracker->queue[last].gesture == KEY_GESTURE_REPEAT) {
            tracker->queue[last].repeats = tracker->states[key].repeats;
            return;
        }
    }
    if (tracker->queue_length == KEY_TRACKER_QUEUE_CAPACITY) {
        return;
    }
    uint32_t slot = (tracker->queue_start + tracker->queue_length) % KEY_TRACKER_QUEUE_CAPACITY;
    tracker->queue[slot] = (KeyEvent){.key = key,
                                      .gesture = gesture,
                                      .repeats = tracker->states[key].repeats,
                                      .chord = tracker->states[key].chord};
    tracker->queue_length++;
}

void key_tracker_init(KeyTracker *tracker, const KeyConfig *config)
{
    *tracker = (KeyTracker){.config = *config};
    tracker->config.timing.long_press_ms = supported_delay(config->timing.long_press_ms);
    tracker->config.timing.repeat_delay_ms = supported_delay(config->timing.repeat_delay_ms);
    tracker->config.timing.repeat_interval_ms = supported_delay(config->timing.repeat_interval_ms);
    for (int key = 0; key < KEY_COUNT; key++) {
        tracker->config.long_press_ms[key] = supported_delay(config->long_press_ms[key]);
    }
}

void key_tracker_set_behavior(KeyTracker *tracker, Key key, KeyBehavior behavior)
{
    tracker->config.behaviors[key] = behavior;
}

static void press(KeyTracker *tracker, Key key, uint32_t now_ms, uint8_t chord)
{
    KeyState *state = &tracker->states[key];
    if (state->held) {
        return;
    }
    *state = (KeyState){
        .held = true,
        .chord = chord,
        .behavior = (uint8_t)(chord != KEY_CHORD_NONE ? KEY_BEHAVIOR_TAP_HOLD_REPEAT
                                                      : tracker->config.behaviors[key]),
        .pressed_at_ms = now_ms,
        .next_repeat_at_ms = now_ms + tracker->config.timing.repeat_delay_ms,
    };
    if (repeats(tracker, key)) {
        push_event(tracker, key, KEY_GESTURE_PRESS);
    }
}

void key_tracker_key_down(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    press(tracker, key, now_ms, KEY_CHORD_NONE);
}

void key_tracker_chord_down(KeyTracker *tracker, Key key, uint32_t now_ms, uint8_t chord)
{
    press(tracker, key, now_ms, chord);
}

void key_tracker_resume(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    press(tracker, key, now_ms, KEY_CHORD_NONE);
    tracker->states[key].hold_only = true;
}

void key_tracker_key_up(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    if (!state->held) {
        return;
    }
    if (!repeats(tracker, key) && !state->long_press_sent && !state->hold_only) {
        uint32_t queued = tracker->queue_length;
        push_event(tracker, key,
                   long_press_due(tracker, key, now_ms) ? KEY_GESTURE_LONG_PRESS
                                                        : KEY_GESTURE_PRESS);
        state->release_queued = tracker->queue_length != queued;
    }
    state->held = false;
}

void key_tracker_cancel(KeyTracker *tracker, Key key)
{
    tracker->states[key] = (KeyState){0};
}

void key_tracker_forget(KeyTracker *tracker, Key key)
{
    uint32_t last =
        (tracker->queue_start + tracker->queue_length + KEY_TRACKER_QUEUE_CAPACITY - 1) %
        KEY_TRACKER_QUEUE_CAPACITY;
    if (tracker->states[key].release_queued && tracker->queue_length > 0 &&
        tracker->queue[last].key == key) {
        tracker->queue_length--;
    }
    key_tracker_cancel(tracker, key);
}

void key_tracker_release_all(KeyTracker *tracker)
{
    for (int key = 0; key < KEY_COUNT; key++) {
        tracker->states[key] = (KeyState){0};
    }
    tracker->queue_length = 0;
}

static void update_repeat(KeyTracker *tracker, Key key, uint32_t now_ms)
{
    KeyState *state = &tracker->states[key];
    if (!deadline_reached(now_ms, state->next_repeat_at_ms)) {
        return;
    }
    if (state->repeats < UINT8_MAX) {
        state->repeats++;
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
    state->next_repeat_at_ms = now_ms + tracker->config.timing.repeat_interval_ms;
}

void key_tracker_update(KeyTracker *tracker, uint32_t now_ms)
{
    for (int index = 0; index < KEY_COUNT; index++) {
        Key key = (Key)index;
        if (!tracker->states[key].held) {
            continue;
        }
        bool repeating = repeats(tracker, key) ||
                         (repeats_after_hold(tracker, key) && tracker->states[key].long_press_sent);
        if (repeating) {
            update_repeat(tracker, key, now_ms);
        } else if (behavior_of(tracker, key) != KEY_BEHAVIOR_HOLD_AT_RELEASE) {
            update_long_press(tracker, key, now_ms);
        }
    }
}

bool key_tracker_next_event(KeyTracker *tracker, KeyEvent *event)
{
    while (tracker->queue_length > 0) {
        *event = tracker->queue[tracker->queue_start];
        tracker->queue_start = (tracker->queue_start + 1) % KEY_TRACKER_QUEUE_CAPACITY;
        tracker->queue_length--;
        if (event->gesture != KEY_GESTURE_REPEAT || !repeats(tracker, event->key) ||
            tracker->states[event->key].held) {
            return true;
        }
    }
    return false;
}
