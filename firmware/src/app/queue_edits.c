#include "queue_edits.h"

static void move_to(UpNext *up_next, uint16_t from, uint16_t to)
{
    uint16_t track = up_next->tracks[from];
    while (from != to) {
        uint16_t next = from < to ? (uint16_t)(from + 1) : (uint16_t)(from - 1);
        up_next->tracks[from] = up_next->tracks[next];
        from = next;
    }
    up_next->tracks[to] = track;
}

bool up_next_edit(UpNext *up_next, uint16_t index, UpNextEdit edit)
{
    if (edit == UP_NEXT_CLEAR) {
        up_next_clear(up_next);
        return true;
    }
    if (index >= up_next->count) {
        return false;
    }
    switch (edit) {
    case UP_NEXT_MOVE_FIRST:
        move_to(up_next, index, 0);
        return true;
    case UP_NEXT_MOVE_EARLIER:
        if (index == 0) {
            return false;
        }
        move_to(up_next, index, (uint16_t)(index - 1));
        return true;
    case UP_NEXT_MOVE_LATER:
        if (index + 1 >= up_next->count) {
            return false;
        }
        move_to(up_next, index, (uint16_t)(index + 1));
        return true;
    case UP_NEXT_REMOVE:
        move_to(up_next, index, (uint16_t)(up_next->count - 1));
        up_next->count--;
        return true;
    case UP_NEXT_CLEAR:
    case UP_NEXT_EDIT_COUNT:
        break;
    }
    return false;
}
