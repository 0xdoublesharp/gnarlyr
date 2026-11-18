#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/key_scanner.h"

typedef struct KeyLadderModel {
    KeyScanner scanner;
    uint8_t held;
    uint16_t reading;
} KeyLadderModel;

void key_ladder_model_init(KeyLadderModel *model);
void key_ladder_model_set(KeyLadderModel *model, Key key, bool pressed, uint32_t time_ms);
bool key_ladder_model_next(KeyLadderModel *model, HalKeyTransition *transition);
