#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/dsd_stream.h"
#include "fuzz_source.h"

typedef struct MemoryFile {
    const uint8_t *bytes;
    uint32_t size;
} MemoryFile;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool read_memory(void *context, uint32_t offset, void *destination, uint32_t length)
{
    const MemoryFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(destination, file->bytes + offset, length);
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_require(size <= UINT32_MAX);
    MemoryFile file = {data, (uint32_t)size};
    FlacInput input = {read_memory, &file, file.size};
    DsdTrack track;
    memset(&track, 0xA5, sizeof track);
    DsdTrack before = track;
    if (!dsd_probe(input, &track)) {
        fuzz_require(memcmp(&track, &before, sizeof track) == 0);
        return 0;
    }
    fuzz_require(track.channels >= 1 && track.channels <= 2);
    fuzz_require(track.data_offset <= file.size &&
                 track.data_bytes <= file.size - track.data_offset);
    fuzz_require(track.rate == 2822400 || track.rate == 5644800 || track.rate == 11289600);
    return 0;
}
