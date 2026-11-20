#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/ape_track.h"
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

static void check_refused(const ApeTrack *track, const ApeTrack *before)
{
    size_t flag = offsetof(ApeTrack, unsupported);
    const uint8_t *bytes = (const uint8_t *)track;
    ApeTrack expected = *before;
    fuzz_require(bytes[flag] <= 1);
    ((uint8_t *)&expected)[flag] = bytes[flag];
    fuzz_require(memcmp(track, &expected, sizeof expected) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_require(size <= UINT32_MAX);
    MemoryFile file = {data, (uint32_t)size};
    FlacInput input = {read_memory, &file, file.size};
    ApeTrack track;
    memset(&track, 0xA5, sizeof track);
    ApeTrack before = track;
    if (!ape_track_probe(input, &track)) {
        check_refused(&track, &before);
        return 0;
    }
    fuzz_require(ape_track_allowed(&track));
    fuzz_require(track.audio_start <= track.audio_end && track.audio_end <= file.size);
    uint32_t frames = track.frames < 8 ? track.frames : 8;
    for (uint32_t frame = 0; frame < frames; frame++) {
        ApeFrame out;
        if (ape_track_frame(input, &track, frame, &out)) {
            fuzz_require(out.offset <= file.size);
            fuzz_require(out.skip < 4 && out.bytes % 4 == 0);
            fuzz_require((uint64_t)out.offset + out.bytes <= (uint64_t)file.size + 7);
        }
    }
    return 0;
}
