#include "device/audio_tempo.h"

#include <stddef.h>

#include "core/seam.h"

static AudioTempo tempo;

CORE_SEAM bool audio_tempo_stage_start(uint8_t *buffer, uint32_t frames, uint32_t rate,
                                       uint16_t speed)
{
    uint32_t needed = audio_tempo_buffer_frames(rate);
    frames = frames < needed ? frames : needed;
    AudioTempoConfig config = {
        .buffer = {buffer, NULL, (uint16_t)frames, 0}, .rate = rate, .speed = speed};
    tempo = (AudioTempo){0};
    return audio_tempo_start(&tempo, config);
}

CORE_SEAM uint32_t audio_tempo_stage_push(const int32_t *samples, uint32_t frames)
{
    uint32_t taken = 0;
    while (taken < frames && audio_tempo_push(&tempo, samples[0], samples[1])) {
        samples += 2;
        taken++;
    }
    return taken;
}

CORE_SEAM uint32_t audio_tempo_stage_pull(int32_t *samples, uint32_t frames, int shift)
{
    uint32_t made = 0;
    int32_t left;
    int32_t right;
    while (made < frames && audio_tempo_pull(&tempo, &left, &right)) {
        samples[0] = (int32_t)((uint32_t)left << shift);
        samples[1] = (int32_t)((uint32_t)right << shift);
        samples += 2;
        made++;
    }
    return made;
}

CORE_SEAM void audio_tempo_stage_finish(void)
{
    audio_tempo_finish(&tempo);
}

CORE_SEAM uint32_t audio_tempo_stage_pending(uint32_t input)
{
    uint32_t speed = tempo.config.speed;
    if (speed == 0) {
        return 0;
    }
    uint64_t total = tempo.input_frames + input;
    uint64_t limit = (2 * 100 * total + speed) / (2 * speed);
    return (uint32_t)(limit - tempo.output_frames);
}
