#pragma once

#include "playback/up_next.h"

typedef enum UpNextEdit {
    UP_NEXT_MOVE_FIRST,
    UP_NEXT_MOVE_EARLIER,
    UP_NEXT_MOVE_LATER,
    UP_NEXT_REMOVE,
    UP_NEXT_CLEAR,
    UP_NEXT_EDIT_COUNT
} UpNextEdit;

bool up_next_edit(UpNext *up_next, uint16_t index, UpNextEdit edit);
