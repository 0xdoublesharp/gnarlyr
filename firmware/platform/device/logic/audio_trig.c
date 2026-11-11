#include "device/audio_math.h"

#include "audio_series.h"

int64_t fixed_cosine(int64_t angle)
{
    return audio_series_sum(angle, 0);
}

int64_t fixed_sine(int64_t angle)
{
    return audio_series_sum(angle, 1);
}
