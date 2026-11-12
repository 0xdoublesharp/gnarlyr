#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/wav_source.h"
#include "fuzz_source.h"

enum {
    FRAME_LIMIT = 32,
};

typedef struct MemoryFile {
    const uint8_t *bytes;
    uint32_t size;
    uint32_t fail_at;
    uint32_t closes;
    bool fail_reads;
} MemoryFile;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t get32(const uint8_t *data, size_t size, size_t offset)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4 && offset + index < size; index++) {
        value |= (uint32_t)data[offset + index] << (index * 8);
    }
    return value;
}

static bool read_file(void *context, uint32_t offset, void *out, uint32_t length)
{
    MemoryFile *file = context;
    if (offset > file->size || length > file->size - offset ||
        (file->fail_reads && offset <= file->fail_at && file->fail_at - offset < length)) {
        return false;
    }
    memcpy(out, file->bytes + offset, length);
    return true;
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    MemoryFile *file = context;
    (void)path;
    *input = (FlacInput){read_file, file, file->size};
    return true;
}

static void close_file(void *context)
{
    MemoryFile *file = context;
    file->closes++;
}

static void exercise(MemoryFile *file, uint32_t start_ms)
{
    static WavBuffers buffers;
    WavSource source;
    wav_source_init(&source,
                    (FlacFileOpener){.open = open_file, .close = close_file, .context = file},
                    &buffers);
    AudioSource audio = wav_source_interface(&source);
    AudioTrackInfo track;
    if (!audio.open(audio.context, "input.wav", start_ms, &track)) {
        fuzz_require(file->closes == 1);
        audio.close(audio.context);
        fuzz_require(file->closes == 1);
        return;
    }
    fuzz_require(track.codec == AUDIO_CODEC_WAV);
    fuzz_require(track.stream.channels >= 1 && track.stream.channels <= 2);
    fuzz_require(track.stream.bits_per_sample == 8 || track.stream.bits_per_sample == 16 ||
                 track.stream.bits_per_sample == 24 || track.stream.bits_per_sample == 32);
    fuzz_require(track.stream.sample_rate != 0);
    uint32_t previous = 0;
    bool first = true;
    for (int step = 0; step < FRAME_LIMIT; step++) {
        AudioFrame frame;
        AudioFrameResult result = audio.next(audio.context, &frame);
        fuzz_require(result >= AUDIO_FRAME_READY && result <= AUDIO_FRAME_ERROR);
        if (result != AUDIO_FRAME_READY) {
            break;
        }
        fuzz_require(frame.samples > 0 && frame.samples <= WAV_SOURCE_BLOCK_FRAMES);
        fuzz_require(frame.first_sample <= track.stream.total_samples &&
                     frame.samples <= track.stream.total_samples - frame.first_sample);
        fuzz_require(first || frame.first_sample == previous);
        fuzz_require(frame.bits == track.stream.bits_per_sample);
        fuzz_require(frame.left != NULL && frame.right != NULL);
        if (track.stream.channels == 1) {
            fuzz_require(frame.left == frame.right);
        }
        previous = frame.first_sample + frame.samples;
        first = false;
        audio.release(audio.context);
    }
    audio.close(audio.context);
    fuzz_require(file->closes == 1);
    audio.close(audio.context);
    fuzz_require(file->closes == 1);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    uint32_t start = get32(data, size, 0);
    MemoryFile readable = {.bytes = data, .size = (uint32_t)size};
    exercise(&readable, start);
    MemoryFile failing = {.bytes = data,
                          .size = (uint32_t)size,
                          .fail_at =
                              size == 0 ? 0 : get32(data, size, size > 4 ? size - 4 : 0) % size,
                          .fail_reads = true};
    exercise(&failing, start);
    return 0;
}
