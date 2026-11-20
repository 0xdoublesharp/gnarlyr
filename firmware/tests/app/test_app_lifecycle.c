#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

enum {
    GENERATED_INPUTS = 48,
    INPUT_BYTES = 96
};

static const uint8_t DAC_THEN_START_VOLUME[] = {0,  0x30, 102, 0, 0,  0,  0,  0,  2,
                                                13, 20,   0,   0, 32, 64, 13, 20, 32};
static const uint8_t EQUALIZER_ACROSS_USB[] = {0,  0x30, 13, 17, 32, 0,  32, 5,
                                               32, 32,   34, 64, 13, 17, 32, 34};
static const uint8_t SHORTCUTS[] = {0, 0x32, 32, 0, 0, 0, 0, 0, 0, 0, 32, 32, 4, 10, 4, 21, 4, 4};

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void replay(const uint8_t *data, size_t size, const char *name)
{
    printf("replay %s (%zu bytes)\n", name, size);
    fflush(stdout);
    LLVMFuzzerTestOneInput(data, size);
}

int main(int argc, char **argv)
{
    LLVMFuzzerInitialize(&argc, &argv);
    replay(DAC_THEN_START_VOLUME, sizeof DAC_THEN_START_VOLUME, "dac then start volume");
    replay(EQUALIZER_ACROSS_USB, sizeof EQUALIZER_ACROSS_USB, "equalizer across usb");
    replay(SHORTCUTS, sizeof SHORTCUTS, "shortcuts");
    const char *count_text = getenv("ECHO_LIFECYCLE_INPUTS");
    int count = count_text != NULL ? atoi(count_text) : GENERATED_INPUTS;
    uint32_t state = 0x6c69666eu;
    const char *seed_text = getenv("ECHO_LIFECYCLE_SEED");
    if (seed_text != NULL) {
        state = (uint32_t)strtoul(seed_text, NULL, 0);
        state = state != 0 ? state : 1u;
    }
    for (int input = 0; input < count; input++) {
        uint8_t bytes[INPUT_BYTES];
        uint32_t start = state;
        for (size_t index = 0; index < sizeof bytes; index++) {
            bytes[index] = (uint8_t)next_random(&state);
        }
        char name[32];
        snprintf(name, sizeof name, "generated %#x", start);
        replay(bytes, sizeof bytes, name);
    }
    printf("%d inputs passed\n", count + 3);
    return 0;
}
