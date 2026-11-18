#include "sim_input.h"

#include <stdbool.h>
#include <stddef.h>

#include "device/key_ladder.h"
#include "device/key_ladder_model.h"
#include "hal/input.h"

enum {
    TRANSITION_CAPACITY = 32
};

typedef struct KeyBinding {
    SDL_Keycode keycode;
    Key key;
} KeyBinding;

typedef struct TransitionQueue {
    HalKeyTransition items[TRANSITION_CAPACITY];
    int start;
    int length;
} TransitionQueue;

static const KeyBinding KEY_BINDINGS[] = {
    {SDLK_UP, KEY_VOLUME_DOWN}, {SDLK_DOWN, KEY_VOLUME_UP}, {SDLK_RETURN, KEY_PLAY},
    {SDLK_SPACE, KEY_PLAY},     {SDLK_m, KEY_MENU},         {SDLK_TAB, KEY_MENU},
    {SDLK_BACKSPACE, KEY_BACK}, {SDLK_ESCAPE, KEY_BACK},    {SDLK_p, KEY_POWER},
};

static TransitionQueue queue;
static KeyLadderModel ladder;
static bool ladder_ready;

static bool find_binding(SDL_Keycode keycode, Key *key)
{
    for (size_t index = 0; index < sizeof KEY_BINDINGS / sizeof KEY_BINDINGS[0]; index++) {
        if (KEY_BINDINGS[index].keycode == keycode) {
            *key = KEY_BINDINGS[index].key;
            return true;
        }
    }
    return false;
}

static void enqueue(HalKeyTransition transition)
{
    if (queue.length == TRANSITION_CAPACITY) {
        return;
    }
    queue.items[(queue.start + queue.length) % TRANSITION_CAPACITY] = transition;
    queue.length++;
}

void sim_input_handle_event(const SDL_Event *event)
{
    bool is_key_event = event->type == SDL_KEYDOWN || event->type == SDL_KEYUP;
    if (!is_key_event || event->key.repeat != 0) {
        return;
    }
    Key key;
    if (!find_binding(event->key.keysym.sym, &key)) {
        return;
    }
    sim_input_inject(key, event->type == SDL_KEYDOWN, event->key.timestamp);
}

void sim_input_inject(Key key, bool pressed, uint32_t time_ms)
{
    if (!ladder_ready) {
        key_ladder_model_init(&ladder);
        ladder_ready = true;
    }
    key_ladder_model_set(&ladder, key, pressed, time_ms);
    HalKeyTransition transition;
    while (key_ladder_model_next(&ladder, &transition)) {
        enqueue(transition);
    }
}

bool hal_input_next_transition(HalKeyTransition *transition)
{
    if (queue.length == 0) {
        return false;
    }
    *transition = queue.items[queue.start];
    queue.start = (queue.start + 1) % TRANSITION_CAPACITY;
    queue.length--;
    return true;
}

bool hal_input_waiting(void)
{
    return queue.length != 0;
}

bool hal_input_loader_keys_held(void)
{
    return false;
}

bool hal_input_next_headset_command(HalHeadsetCommand *command)
{
    (void)command;
    return false;
}

HalHeadsetReading hal_input_headset_reading(void)
{
    return (HalHeadsetReading){.known = false};
}

uint16_t hal_input_key_adc(void)
{
    return ladder_ready ? ladder.reading : KEY_LADDER_IDLE_READING;
}

bool hal_input_decode_key(uint16_t reading, Key *key)
{
    return key_ladder_decode(reading, key);
}
