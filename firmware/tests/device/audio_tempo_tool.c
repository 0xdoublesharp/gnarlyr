#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "device/audio_tempo.h"

static AudioTempo tempo;
static uint8_t workspace[6002 * AUDIO_TEMPO_FRAME_BYTES];

static bool drain(FILE *output)
{
    int32_t samples[2];
    while (audio_tempo_pull(&tempo, &samples[0], &samples[1])) {
        if (fwrite(samples, sizeof samples, 1, output) != 1) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        return 2;
    }
    AudioTempoConfig config = {.buffer = {workspace, NULL, 6002, 0},
                               .rate = (uint32_t)strtoul(argv[1], NULL, 10),
                               .speed = (uint16_t)strtoul(argv[2], NULL, 10)};
    if (!audio_tempo_start(&tempo, config)) {
        return 2;
    }
    FILE *input = fopen(argv[3], "rb");
    FILE *output = fopen(argv[4], "wb");
    if (input == NULL || output == NULL) {
        return 2;
    }
    int32_t samples[2];
    size_t bytes;
    while ((bytes = fread(samples, 1, sizeof samples, input)) != 0) {
        if (bytes != sizeof samples) {
            return 3;
        }
        if (!audio_tempo_push(&tempo, samples[0], samples[1])) {
            if (!drain(output) || !audio_tempo_push(&tempo, samples[0], samples[1])) {
                return 3;
            }
        }
        if (!drain(output)) {
            return 3;
        }
    }
    if (ferror(input) || fclose(input) != 0) {
        return 3;
    }
    audio_tempo_finish(&tempo);
    bool complete = drain(output) && audio_tempo_done(&tempo);
    return fclose(output) == 0 && complete ? 0 : 3;
}
