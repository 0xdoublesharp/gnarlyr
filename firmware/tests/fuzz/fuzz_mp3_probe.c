#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/mp3_probe.h"
#include "fuzz_source.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static FlacInput input_of(FuzzSource *source)
{
    ByteSource bytes = fuzz_source(source);
    return (FlacInput){.read = bytes.read, .context = bytes.context, .size = source->size};
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    uint8_t signature[MP3_SIGNATURE_BYTES] = {0};
    size_t copied = size < sizeof signature ? size : sizeof signature;
    memcpy(signature, data, copied);
    mp3_signature(signature);
    FuzzSource source = {.bytes = data, .size = (uint32_t)size};
    Mp3Stream stream;
    if (!mp3_probe(input_of(&source), &stream)) {
        return 0;
    }
    fuzz_require(stream.channels >= 1 && stream.channels <= 2);
    fuzz_require(stream.sample_rate > 0);
    fuzz_require(stream.audio_start <= stream.audio_end && stream.audio_end <= size);
    fuzz_require(mp3_playable_samples(&stream) <= stream.total_samples);
    uint32_t sample = (uint32_t)size;
    for (size_t index = 0; index < copied; index++) {
        sample = sample * 33u + signature[index];
    }
    mp3_seek_offset(&stream, sample);
    mp3_seek_offset(&stream, UINT32_MAX);
    return 0;
}
