#include "device/headset_remote.h"

enum {
    PLAY_LIMIT = 16,
    VOLUME_UP_LIMIT = 46,
    VOLUME_DOWN_LIMIT = 101
};

static const HalHeadsetCommand COMMAND_BY_CLICKS[HEADSET_REMOTE_MAX_CLICKS] = {
    HAL_HEADSET_TOGGLE_PAUSE,
    HAL_HEADSET_NEXT,
    HAL_HEADSET_PREVIOUS,
};

HalHeadsetButton headset_remote_button(uint16_t level)
{
    if (level < PLAY_LIMIT) {
        return HAL_HEADSET_BUTTON_PLAY;
    }
    if (level < VOLUME_UP_LIMIT) {
        return HAL_HEADSET_BUTTON_VOLUME_UP;
    }
    if (level < VOLUME_DOWN_LIMIT) {
        return HAL_HEADSET_BUTTON_VOLUME_DOWN;
    }
    return HAL_HEADSET_BUTTON_NONE;
}

void headset_remote_init(HeadsetRemote *remote)
{
    *remote = (HeadsetRemote){.plugged = false};
}

static void push(HeadsetRemote *remote, HalHeadsetCommand command)
{
    if (remote->count == HEADSET_REMOTE_QUEUE_CAPACITY) {
        return;
    }
    uint32_t tail = (uint32_t)(remote->head + remote->count) % HEADSET_REMOTE_QUEUE_CAPACITY;
    remote->queue[tail] = command;
    remote->count++;
}

static void forget_buttons(HeadsetRemote *remote)
{
    remote->candidate = HAL_HEADSET_BUTTON_NONE;
    remote->held = HAL_HEADSET_BUTTON_NONE;
    remote->clicks = 0;
}

static void follow_run(HeadsetRemote *remote, uint16_t level, uint32_t time_ms)
{
    bool play = headset_remote_button(level) == HAL_HEADSET_BUTTON_PLAY;
    if (!remote->plugged || play != remote->run_play) {
        remote->run_play = play;
        remote->run_ms = time_ms;
    }
}

static void decide_plug(HeadsetRemote *remote, uint32_t time_ms)
{
    uint32_t run = time_ms - remote->run_ms;
    if (!remote->decided && run >= HEADSET_REMOTE_SETTLE_MS) {
        remote->decided = true;
        remote->no_mic = remote->run_play;
    } else if (remote->no_mic && !remote->run_play && run >= HEADSET_REMOTE_SETTLE_MS) {
        remote->no_mic = false;
    } else if (!remote->no_mic && remote->run_play && run >= HEADSET_REMOTE_PLUG_CHANGE_MS) {
        remote->no_mic = true;
    }
}

static bool settled(HeadsetRemote *remote, uint16_t level, bool plugged, uint32_t time_ms)
{
    if (!plugged) {
        remote->plugged = false;
        remote->decided = false;
        remote->no_mic = false;
        forget_buttons(remote);
        return false;
    }
    follow_run(remote, level, time_ms);
    remote->plugged = true;
    decide_plug(remote, time_ms);
    if (!headset_remote_listening(remote)) {
        forget_buttons(remote);
        return false;
    }
    return true;
}

static void release(HeadsetRemote *remote, uint32_t time_ms)
{
    if (remote->long_sent) {
        return;
    }
    switch (remote->held) {
    case HAL_HEADSET_BUTTON_PLAY:
        if (remote->clicks <= HEADSET_REMOTE_MAX_CLICKS) {
            remote->clicks++;
        }
        remote->click_ms = time_ms;
        break;
    case HAL_HEADSET_BUTTON_VOLUME_UP:
        push(remote, HAL_HEADSET_VOLUME_UP);
        break;
    case HAL_HEADSET_BUTTON_VOLUME_DOWN:
        push(remote, HAL_HEADSET_VOLUME_DOWN);
        break;
    case HAL_HEADSET_BUTTON_NONE:
        break;
    }
}

static void follow_button(HeadsetRemote *remote, HalHeadsetButton button, uint32_t time_ms)
{
    bool steady = button == remote->candidate;
    remote->candidate = button;
    if (!steady || button == remote->held) {
        return;
    }
    release(remote, time_ms);
    remote->held = button;
    remote->held_ms = time_ms;
    remote->long_sent = false;
}

static void follow_hold(HeadsetRemote *remote, uint32_t time_ms)
{
    if (remote->held == HAL_HEADSET_BUTTON_NONE || remote->long_sent ||
        time_ms - remote->held_ms < HEADSET_REMOTE_LONG_PRESS_MS) {
        return;
    }
    remote->long_sent = true;
    if (remote->held == HAL_HEADSET_BUTTON_VOLUME_UP) {
        push(remote, HAL_HEADSET_NEXT);
    } else if (remote->held == HAL_HEADSET_BUTTON_VOLUME_DOWN) {
        push(remote, HAL_HEADSET_PREVIOUS);
    } else {
        remote->clicks = 0;
    }
}

static void finish_clicks(HeadsetRemote *remote, uint32_t time_ms)
{
    if (remote->clicks == 0 || remote->held != HAL_HEADSET_BUTTON_NONE ||
        time_ms - remote->click_ms < HEADSET_REMOTE_CLICK_GAP_MS) {
        return;
    }
    if (remote->clicks <= HEADSET_REMOTE_MAX_CLICKS) {
        push(remote, COMMAND_BY_CLICKS[remote->clicks - 1]);
    }
    remote->clicks = 0;
}

void headset_remote_sample(HeadsetRemote *remote, uint16_t level, bool plugged, uint32_t time_ms)
{
    if (!settled(remote, level, plugged, time_ms)) {
        return;
    }
    follow_button(remote, headset_remote_button(level), time_ms);
    follow_hold(remote, time_ms);
    finish_clicks(remote, time_ms);
}

bool headset_remote_next(HeadsetRemote *remote, HalHeadsetCommand *command)
{
    if (remote->count == 0) {
        return false;
    }
    *command = remote->queue[remote->head];
    remote->head = (uint8_t)((remote->head + 1) % HEADSET_REMOTE_QUEUE_CAPACITY);
    remote->count--;
    return true;
}

bool headset_remote_no_mic(const HeadsetRemote *remote)
{
    return remote->plugged && remote->decided && remote->no_mic;
}

bool headset_remote_listening(const HeadsetRemote *remote)
{
    return remote->plugged && remote->decided && !remote->no_mic;
}
