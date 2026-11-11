#include "core/headphone_audio.h"

#include <stdbool.h>

uint8_t headphone_audio_switch_volume(HeadphoneAudio before, HeadphoneAudio after, uint8_t level)
{
    uint8_t previous = headphone_audio_limit(before, level);
    uint8_t ceiling = headphone_audio_limit(after, previous);
    uint64_t scale = UINT64_C(1) << 32;
    uint8_t next = previous;
    while (next != 0) {
        bool safe = (uint64_t)after.left * scale <= (uint64_t)before.left << 32 &&
                    (uint64_t)after.right * scale <= (uint64_t)before.right << 32;
        if (safe && next <= ceiling) {
            return next;
        }
        next--;
        scale = (scale * 58410 + REPLAY_GAIN_UNITY - 1) >> 16;
    }
    return 0;
}
