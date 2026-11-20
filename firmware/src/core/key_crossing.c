#include "core/key_crossing.h"

enum {
    CROSSING_IDLE,
    CROSSING_RELEASED,
    CROSSING_UNDER_PLAY,
    CROSSING_LEFT_PLAY
};

static bool volume_key(Key key)
{
    return key == KEY_VOLUME_UP || key == KEY_VOLUME_DOWN;
}

static void emit(KeyCrossingOut *out, Key key, bool pressed, uint32_t time_ms)
{
    if (out->count < KEY_CROSSING_OUT_CAPACITY) {
        out->steps[out->count++] = (KeyStep){key, pressed, time_ms};
    }
}

static void replay(KeyCrossing *crossing, KeyCrossingOut *out)
{
    emit(out, (Key)crossing->from, false, crossing->from_ms);
    if (crossing->state != CROSSING_RELEASED) {
        emit(out, KEY_PLAY, true, crossing->from_ms);
    }
    if (crossing->state == CROSSING_LEFT_PLAY) {
        emit(out, KEY_PLAY, false, crossing->play_ms);
    }
    crossing->state = CROSSING_IDLE;
}

static void leave_play(KeyCrossing *crossing, KeyCrossingOut *out)
{
    if (volume_key((Key)crossing->from)) {
        emit(out, (Key)crossing->from, false, crossing->from_ms);
        crossing->state = CROSSING_IDLE;
        return;
    }
    replay(crossing, out);
}

static void take_idle(KeyCrossing *crossing, KeyStep step, KeyCrossingRules rules,
                      KeyCrossingOut *out)
{
    bool watched = step.key == KEY_MENU ? rules.menu : volume_key(step.key) && rules.volume;
    if (!step.pressed && watched) {
        *crossing = (KeyCrossing){CROSSING_RELEASED, (uint8_t)step.key, step.time_ms, 0};
        return;
    }
    emit(out, step.key, step.pressed, step.time_ms);
}

static bool arrives(const KeyCrossing *crossing, KeyStep step)
{
    Key from = (Key)crossing->from;
    return (from == KEY_MENU && volume_key(step.key)) ||
           (volume_key(from) && (step.key == KEY_MENU || step.key == from));
}

static bool settle_left(KeyCrossing *crossing, KeyStep step, KeyCrossingOut *out)
{
    if (!step.pressed || step.time_ms != crossing->play_ms || !arrives(crossing, step)) {
        return false;
    }
    if (step.key != (Key)crossing->from) {
        emit(out, (Key)crossing->from, false, crossing->from_ms);
        emit(out, step.key, true, crossing->from_ms);
    }
    crossing->state = CROSSING_IDLE;
    return true;
}

static bool held_back(KeyCrossing *crossing, KeyStep step, KeyCrossingOut *out)
{
    bool play = step.key == KEY_PLAY;
    if (crossing->state == CROSSING_RELEASED && play && step.pressed &&
        step.time_ms == crossing->from_ms) {
        crossing->state = CROSSING_UNDER_PLAY;
        return true;
    }
    if (crossing->state == CROSSING_UNDER_PLAY && play && !step.pressed &&
        step.time_ms - crossing->from_ms < KEY_CROSSING_WINDOW_MS) {
        crossing->state = CROSSING_LEFT_PLAY;
        crossing->play_ms = step.time_ms;
        return true;
    }
    return crossing->state == CROSSING_LEFT_PLAY && settle_left(crossing, step, out);
}

void key_crossing_reset(KeyCrossing *crossing)
{
    *crossing = (KeyCrossing){0};
}

void key_crossing_take(KeyCrossing *crossing, KeyStep step, KeyCrossingRules rules,
                       KeyCrossingOut *out)
{
    if (crossing->state != CROSSING_IDLE && held_back(crossing, step, out)) {
        return;
    }
    if (crossing->state == CROSSING_LEFT_PLAY) {
        leave_play(crossing, out);
    } else if (crossing->state != CROSSING_IDLE) {
        replay(crossing, out);
    }
    take_idle(crossing, step, rules, out);
}

void key_crossing_settle(KeyCrossing *crossing, uint32_t now_ms, KeyCrossingOut *out)
{
    if (crossing->state == CROSSING_RELEASED) {
        replay(crossing, out);
    } else if (crossing->state == CROSSING_LEFT_PLAY) {
        leave_play(crossing, out);
    } else if (crossing->state == CROSSING_UNDER_PLAY &&
               now_ms - crossing->from_ms >= KEY_CROSSING_WINDOW_MS) {
        replay(crossing, out);
    }
}
