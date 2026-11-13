#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/asf_stream.h"
#include "device/asf_track.h"
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
    AsfTrack track;
    memset(&track, 0xA5, sizeof track);
    AsfTrack before = track;
    if (!asf_track_probe(input, &track)) {
        fuzz_require(memcmp(&track, &before, sizeof track) == 0);
        return 0;
    }
    fuzz_require(track.data_offset <= track.data_end && track.data_end <= file.size);
    fuzz_require(track.channels >= 1 && track.channels <= 2);
    fuzz_require(track.rate >= 8000 && track.rate <= 48000);
    static AsfStream stream;
    if (asf_stream_start_at(&stream, input, &track, size == 0 ? 0 : data[0] * 10u)) {
        uint8_t records[8 * 37];
        uint32_t used = 0;
        while (used < sizeof records && asf_stream_read(&stream, used, records + used, 37)) {
            used += 37;
        }
    }
    return 0;
}
