#include "settings/rocker.h"

bool rocker_swapped_in(Orientation orientation)
{
    return orientation == ORIENTATION_PORTRAIT;
}

bool rocker_volume_swapped_in(Orientation orientation)
{
    return orientation == ORIENTATION_PORTRAIT_FLIPPED;
}

Key rocker_key(Key key, bool swapped)
{
    if (!swapped) {
        return key;
    }
    if (key == KEY_VOLUME_UP) {
        return KEY_VOLUME_DOWN;
    }
    return key == KEY_VOLUME_DOWN ? KEY_VOLUME_UP : key;
}
