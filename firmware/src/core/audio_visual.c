#include "core/audio_visual.h"

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

static void reverse(AudioVisualWork *work)
{
    unsigned reversed = 0;
    for (unsigned index = 1; index < AUDIO_VISUAL_FFT; index++) {
        unsigned bit = AUDIO_VISUAL_FFT / 2;
        while ((reversed & bit) != 0) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed) {
            int32_t value = work->real[index];
            work->real[index] = work->real[reversed];
            work->real[reversed] = value;
        }
    }
}

void audio_visual_fft(AudioVisualWork *work)
{
    static const int32_t COSINE[] = {-32768, 0, 23170, 30274, 32138, 32610, 32729, 32758};
    static const int32_t SINE[] = {0, -32768, -23170, -12540, -6393, -3212, -1608, -804};
    reverse(work);
    for (int stage = 0, span = 2; span <= AUDIO_VISUAL_FFT; stage++, span *= 2) {
        int32_t real = 32768;
        int32_t imag = 0;
        for (int offset = 0; offset < span / 2; offset++) {
            for (int first = offset; first < AUDIO_VISUAL_FFT; first += span) {
                int second = first + span / 2;
                int32_t re = (real * work->real[second] - imag * work->imag[second]) / 32768;
                int32_t im = (real * work->imag[second] + imag * work->real[second]) / 32768;
                work->real[second] = (work->real[first] - re) / 2;
                work->imag[second] = (work->imag[first] - im) / 2;
                work->real[first] = (work->real[first] + re) / 2;
                work->imag[first] = (work->imag[first] + im) / 2;
            }
            int32_t next = (real * COSINE[stage] - imag * SINE[stage]) / 32768;
            imag = (real * SINE[stage] + imag * COSINE[stage]) / 32768;
            real = next;
        }
    }
}

void audio_visual_needle(AudioVisualNeedle *needle, uint16_t target, uint32_t elapsed_ms)
{
    uint32_t remaining = elapsed_ms > 1000 ? 1000 : elapsed_ms;
    while (remaining > 0) {
        int32_t step = remaining > 2 ? 2 : (int32_t)remaining;
        int32_t error = (int32_t)target * 256 - needle->position;
        needle->velocity +=
            (int32_t)(((int64_t)error * 576 - (int64_t)needle->velocity * 39360) * step / 1000000);
        needle->position += needle->velocity * step;
        if (needle->position < 0) {
            needle->position = 0;
            needle->velocity = 0;
        }
        remaining -= (uint32_t)step;
    }
}

int audio_visual_band_edge(int band)
{
    static const uint8_t EDGES[AUDIO_VISUAL_BANDS + 1] = {1,  2,  3,  4,  5,  7,   10,  14, 19,
                                                          26, 36, 49, 66, 88, 108, 120, 129};
    return EDGES[band < 0 ? 0 : band > AUDIO_VISUAL_BANDS ? AUDIO_VISUAL_BANDS : band];
}

uint8_t audio_visual_db(uint32_t magnitude)
{
    if (magnitude < 16) {
        return 0;
    }
    int bit = 0;
    uint32_t reduced = magnitude;
    while (reduced > 1) {
        reduced >>= 1;
        bit++;
    }
    unsigned level = (unsigned)(bit - 4) * 23 + ((magnitude >> (bit - 4)) & 15) * 23 / 16;
    return (uint8_t)(level > 255 ? 255 : level);
}

static void bands(AudioVisualWork *work, AudioVisualLevels *levels)
{
    audio_visual_fft(work);
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        uint32_t peak = 0;
        for (int bin = audio_visual_band_edge(band); bin < audio_visual_band_edge(band + 1);
             bin++) {
            uint32_t re = (uint32_t)absolute(work->real[bin]);
            uint32_t im = (uint32_t)absolute(work->imag[bin]);
            uint32_t magnitude = re > im ? re + im / 2 : im + re / 2;
            peak = magnitude > peak ? magnitude : peak;
        }
        levels->band[band] = audio_visual_db(peak * 4);
    }
}

AudioVisualLevels audio_visual_measure(AudioVisualWork *work, AudioVisualBuffer buffer,
                                       bool spectrum)
{
    AudioVisualLevels levels = {0};
    if (buffer.words == 0 || buffer.frames == 0 || buffer.count == 0) {
        return levels;
    }
    uint32_t stride = (buffer.rate + 22049) / 22050;
    stride = stride == 0 ? 1 : stride > 9 ? 9 : stride;
    uint32_t count = AUDIO_VISUAL_FFT * stride;
    count = count < buffer.count ? count : buffer.count;
    count = count < buffer.frames ? count : buffer.frames;
    uint32_t cursor = buffer.first % buffer.frames;
    uint32_t sum[2] = {0};
    int32_t mono = 0;
    for (int bin = 0; bin < AUDIO_VISUAL_FFT; bin++) {
        work->real[bin] = 0;
        work->imag[bin] = 0;
    }
    for (uint32_t frame = 0; frame < count; frame++) {
        for (int channel = 0; channel < 2; channel++) {
            int32_t sample = (int32_t)buffer.words[cursor * 2 + (unsigned)channel] >> 16;
            uint16_t magnitude = (uint16_t)absolute(sample);
            sum[channel] += magnitude;
            if (magnitude > levels.peak[channel]) {
                levels.peak[channel] = magnitude;
            }
            mono += sample;
        }
        if (spectrum && (frame + 1) % stride == 0) {
            int bin = (int)(frame / stride);
            int window = AUDIO_VISUAL_FFT / 2 - absolute(bin - AUDIO_VISUAL_FFT / 2);
            work->real[bin] = mono / (int32_t)(stride * 2) * window / (AUDIO_VISUAL_FFT / 2);
            mono = 0;
        }
        if (++cursor == buffer.frames) {
            cursor = 0;
        }
    }
    for (int channel = 0; channel < 2; channel++) {
        levels.mean[channel] = (uint16_t)(sum[channel] / count);
    }
    if (spectrum) {
        bands(work, &levels);
    }
    return levels;
}
