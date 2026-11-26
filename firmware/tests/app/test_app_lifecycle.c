#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

enum {
    GENERATED_INPUTS = 16,
    INPUT_BYTES = 96
};

static const uint8_t DAC_THEN_START_VOLUME[] = {0,  0x30, 102, 0, 0,  0,  0,  0,  2,
                                                13, 20,   0,   0, 32, 64, 13, 20, 32};
static const uint8_t EQUALIZER_ACROSS_USB[] = {0,  0x30, 13, 17, 32, 0,  32, 5,
                                               32, 32,   34, 64, 13, 17, 32, 34};
static const uint8_t SHORTCUTS[] = {0, 0x32, 32, 0, 0, 0, 0, 0, 0, 0, 32, 32, 4, 10, 4, 21, 4, 4};

static const uint8_t HOME_AFTER_USB[] = {0, 0, 0x20, 0x05, 0x42};
static const uint8_t HOME_AFTER_DAC[] = {0, 0, 0x20, 0x06, 0x42};
static const uint8_t PARAMETRIC_ACROSS_USB[] = {0, 0x20, 0x0d, 0x25, 0x05, 0x20};
static const uint8_t UNREADABLE_PROFILES[] = {0x69, 0xf2};
static const uint8_t CORRUPT_PROFILES[] = {0x69, 0xb2, 0x0d, 0x05, 0x20};
static const uint8_t MY_TAGS_ACROSS_COVERS[] = {0xde, 0x81, 0x96, 0xad, 0x73, 0xaa};
static const uint8_t DAC_WITH_DAMAGED_SETTINGS[] = {
    0x00, 0x30, 0x30, 0x00, 0x00, 0xf4, 0x02, 0x00, 0x02, 0x0d, 0x00, 0x20, 0x40, 0x20, 0x19,
    0x20, 0x0c, 0x52, 0xa6, 0xf2, 0x55, 0x00, 0x00, 0x40, 0x0d, 0x13, 0x20, 0x22, 0x20, 0x22};

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

static int replay_hex(const char *text)
{
    uint8_t bytes[INPUT_BYTES * 2];
    size_t size = 0;
    for (const char *at = text; at[0] != '\0' && at[1] != '\0' && size < sizeof bytes; at += 2) {
        char pair[3] = {at[0], at[1], '\0'};
        bytes[size++] = (uint8_t)strtoul(pair, NULL, 16);
    }
    replay(bytes, size, "hex input");
    return 0;
}

int main(int argc, char **argv)
{
    LLVMFuzzerInitialize(&argc, &argv);
    const char *hex = getenv("ECHO_LIFECYCLE_INPUT");
    if (hex != NULL) {
        return replay_hex(hex);
    }
    replay(DAC_THEN_START_VOLUME, sizeof DAC_THEN_START_VOLUME, "dac then start volume");
    replay(EQUALIZER_ACROSS_USB, sizeof EQUALIZER_ACROSS_USB, "equalizer across usb");
    replay(SHORTCUTS, sizeof SHORTCUTS, "shortcuts");
    replay(HOME_AFTER_USB, sizeof HOME_AFTER_USB, "home after a storage session");
    replay(HOME_AFTER_DAC, sizeof HOME_AFTER_DAC, "home after a DAC session");
    replay(PARAMETRIC_ACROSS_USB, sizeof PARAMETRIC_ACROSS_USB, "parametric editor across usb");
    replay(UNREADABLE_PROFILES, sizeof UNREADABLE_PROFILES, "unreadable profiles");
    replay(CORRUPT_PROFILES, sizeof CORRUPT_PROFILES, "corrupt profiles");
    replay(MY_TAGS_ACROSS_COVERS, sizeof MY_TAGS_ACROSS_COVERS, "my tags across the cover pass");
    replay(DAC_WITH_DAMAGED_SETTINGS, sizeof DAC_WITH_DAMAGED_SETTINGS,
           "DAC with damaged Settings");
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
    printf("%d inputs passed\n", count + 10);
    return 0;
}
