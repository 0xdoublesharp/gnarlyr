#include "device/key_ladder_model.h"

#include "device/key_ladder.h"

static const uint8_t POWER_MASK = 1u << KEY_POWER;

void key_ladder_model_init(KeyLadderModel *model)
{
    *model = (KeyLadderModel){.reading = KEY_LADDER_IDLE_READING};
    key_scanner_init(&model->scanner);
    key_scanner_sample(&model->scanner, 0, 0);
}

void key_ladder_model_set(KeyLadderModel *model, Key key, bool pressed, uint32_t time_ms)
{
    uint8_t bit = (uint8_t)(1u << key);
    model->held = pressed ? (uint8_t)(model->held | bit) : (uint8_t)(model->held & ~bit);
    model->reading = key_ladder_reading(model->held);
    uint8_t previous = (uint8_t)(model->scanner.stable & ~POWER_MASK);
    uint8_t mask = key_ladder_held_mask(model->reading, previous) | (model->held & POWER_MASK);
    for (int sample = 0; sample < KEY_SCANNER_PRESS_SAMPLES; sample++) {
        key_scanner_sample(&model->scanner, mask, time_ms);
    }
}

void key_ladder_model_sample(KeyLadderModel *model, uint16_t reading, uint32_t time_ms)
{
    uint8_t previous = (uint8_t)(model->scanner.stable & ~POWER_MASK);
    uint8_t mask = key_ladder_held_mask(reading, previous) | (model->held & POWER_MASK);
    key_scanner_sample(&model->scanner, mask, time_ms);
}

bool key_ladder_model_next(KeyLadderModel *model, HalKeyTransition *transition)
{
    return key_scanner_next(&model->scanner, transition);
}
