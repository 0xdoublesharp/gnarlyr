#include "device/audio_resample.h"

#include "audio_resample_table.h"

enum {
    FRACTION_BITS = 30,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608,
    HISTORY_MASK = AUDIO_RESAMPLE_HISTORY - 1,
    TAPS = AUDIO_RESAMPLE_LEFT + AUDIO_RESAMPLE_RIGHT + 1
};

static const uint8_t PHASE_ROWS[7][6] = {
    {0}, {0}, {0, 4}, {0, 3, 5}, {0, 2, 4, 6}, {0}, {0, 1, 3, 4, 5, 7}};

static int32_t round_sample(int64_t value)
{
    value += INT64_C(1) << (FRACTION_BITS - 1);
    int64_t rounded = value >= 0 ? value >> FRACTION_BITS : -((-value - 1) >> FRACTION_BITS) - 1;
    return rounded > SAMPLE_MAX ? SAMPLE_MAX : rounded < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)rounded;
}

static void interpolate(const AudioResample *resample, int32_t *left, int32_t *right)
{
    const int32_t *row = RESAMPLE_COEFFICIENTS[PHASE_ROWS[resample->factor][resample->phase]];
    int64_t a = 0;
    int64_t b = 0;
    uint32_t first = resample->cursor < AUDIO_RESAMPLE_LEFT
                         ? AUDIO_RESAMPLE_LEFT - (uint32_t)resample->cursor
                         : 0;
    uint64_t available = resample->frames - resample->cursor;
    uint32_t limit =
        available > AUDIO_RESAMPLE_RIGHT ? TAPS : AUDIO_RESAMPLE_LEFT + (uint32_t)available;
    uint32_t at = ((uint32_t)resample->cursor - AUDIO_RESAMPLE_LEFT + first) & HISTORY_MASK;
    for (uint32_t tap = first; tap < limit; tap++) {
        a += (int64_t)resample->history[0][at] * row[tap];
        b += (int64_t)resample->history[1][at] * row[tap];
        at = (at + 1) & HISTORY_MASK;
    }
    *left = round_sample(a);
    *right = round_sample(b);
}

bool audio_resample_pull(AudioResample *resample, int32_t *left, int32_t *right)
{
    if (resample->factor == 0 || resample->cursor == resample->frames ||
        (!resample->ended && resample->factor > 1 &&
         resample->frames - resample->cursor <= AUDIO_RESAMPLE_RIGHT)) {
        return false;
    }
    if (resample->factor == 1) {
        uint32_t at = (uint32_t)resample->cursor & HISTORY_MASK;
        *left = resample->history[0][at];
        *right = resample->history[1][at];
    } else {
        interpolate(resample, left, right);
    }
    resample->phase++;
    if (resample->phase == resample->factor) {
        resample->phase = 0;
        resample->cursor++;
    }
    return true;
}
