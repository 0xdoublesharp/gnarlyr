#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "image/cover_image.h"

enum {
    SCRATCH_BYTES = 384 * 1024
};

typedef struct Memory {
    const uint8_t *bytes;
    uint32_t size;
} Memory;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool read_memory(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const Memory *memory = context;
    if (offset > memory->size || length > memory->size - offset) {
        return false;
    }
    memcpy(buffer, memory->bytes + offset, length);
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    static uint8_t scratch[SCRATCH_BYTES];
    static uint16_t full[COVER_GRID_CELLS];
    static uint16_t half[COVER_GRID_HALF_CELLS];
    memset(scratch, 0, sizeof scratch);
    memset(full, 0, sizeof full);
    memset(half, 0, sizeof half);
    Memory memory = {.bytes = data, .size = (uint32_t)size};
    ImageSource source = {read_memory, &memory, 0, memory.size};
    CoverImageResult result = cover_image_decode(source, scratch, sizeof scratch, full, half);
    fuzz_require(result >= COVER_IMAGE_OK && result <= COVER_IMAGE_NO_MEMORY);
    return 0;
}
