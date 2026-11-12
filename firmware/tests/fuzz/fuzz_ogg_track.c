#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/ogg_stream.h"
#include "device/opus_track.h"
#include "fuzz_source.h"

enum {
    READ_BYTES = 512,
    READ_LIMIT = 128,
};

typedef struct MemoryFile {
    const uint8_t *bytes;
    uint32_t size;
    uint32_t fail_at;
    bool failing;
} MemoryFile;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool read_file(void *context, uint32_t offset, void *out, uint32_t length)
{
    MemoryFile *file = context;
    if (offset > file->size || length > file->size - offset ||
        (file->failing && offset <= file->fail_at && file->fail_at - offset < length)) {
        return false;
    }
    memcpy(out, file->bytes + offset, length);
    return true;
}

static FlacInput input(MemoryFile *file)
{
    return (FlacInput){read_file, file, file->size};
}

static uint32_t get32(const uint8_t *data, size_t size, size_t offset)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4 && offset + index < size; index++) {
        value |= (uint32_t)data[offset + index] << (index * 8);
    }
    return value;
}

static void read_stream(OggStream *stream, uint8_t control)
{
    uint8_t bytes[READ_BYTES];
    for (int step = 0; step < READ_LIMIT && !stream->failed && !stream->ended; step++) {
        uint32_t count = 1u + (uint32_t)(control + step) % sizeof bytes;
        if (!ogg_stream_read(stream, stream->position, bytes, count)) {
            break;
        }
        fuzz_require(stream->position <= OGG_VIRTUAL_END);
    }
}

static void exercise_vorbis(FlacInput file, uint8_t control)
{
    OggTrack track;
    if (!vorbis_track_probe(file, &track)) {
        return;
    }
    fuzz_require(track.rate >= 8000 && track.rate <= 192000);
    fuzz_require(track.channels >= 1 && track.channels <= 2);
    fuzz_require(track.samples != 0 && track.stream_end <= file.size);
    uint32_t offset = 0;
    fuzz_require(ogg_track_seek(file, &track, 0, &offset));
    fuzz_require(offset < track.stream_end);
    ogg_track_seek(file, &track, track.samples / 2, &offset);
    ogg_track_seek(file, &track, track.samples, &offset);

    OggStream stream;
    fuzz_require(ogg_stream_start(&stream, file));
    read_stream(&stream, control);
    if (ogg_stream_seek(&stream, file, &track, track.samples / 2)) {
        read_stream(&stream, (uint8_t)(control + 1));
    }

    OggPackets packets;
    ogg_packets_init_first(&packets, file);
    VorbisModes modes = {.count = (uint8_t)(1 + control % 64),
                         .short_block = track.min_block,
                         .long_block = track.max_block};
    for (uint32_t index = 0; index < sizeof modes.long_modes / sizeof modes.long_modes[0];
         index++) {
        modes.long_modes[index] = control * (index + 1u);
    }
    OggTrack candidate = track;
    vorbis_track_origin(&packets, &modes, &candidate);
    ogg_stream_prepare_vorbis(&stream, &candidate, &modes, track.samples / 2);
}

static void exercise_opus(FlacInput file, const uint8_t *data, size_t size, uint8_t control)
{
    opus_track_frames(data, (uint32_t)(size < UINT32_MAX ? size : UINT32_MAX));
    OggTrack track;
    if (!opus_track_probe(file, &track)) {
        return;
    }
    fuzz_require(track.opus && track.rate == OPUS_TRACK_RATE && track.samples != 0);
    uint32_t offset = 0;
    opus_track_seek(file, &track, 0, &offset);
    opus_track_seek(file, &track, track.samples / 2, &offset);
    opus_track_seek(file, &track, track.samples, &offset);
    OggStream stream;
    if (ogg_stream_resume(&stream, file, &track, offset)) {
        read_stream(&stream, control);
    }
}

static void exercise_invalid(FlacInput file, const uint8_t *data, size_t size)
{
    OggTrack track = {.file_bytes = get32(data, size, 0),
                      .data_offset = get32(data, size, 4),
                      .stream_end = get32(data, size, 8),
                      .serial = get32(data, size, 12),
                      .origin = get32(data, size, 16),
                      .samples = get32(data, size, 20),
                      .end_granule = get32(data, size, 24),
                      .min_block = (uint16_t)get32(data, size, 28),
                      .max_block = (uint16_t)get32(data, size, 32),
                      .opus = size > 36 && (data[36] & 1u) != 0};
    uint32_t offset = 0;
    ogg_track_endpoint(file, &track);
    ogg_track_seek(file, &track, get32(data, size, 37), &offset);
    opus_track_seek(file, &track, get32(data, size, 41), &offset);
    OggStream stream;
    if (ogg_stream_resume(&stream, file, &track, get32(data, size, 45))) {
        uint8_t out[16];
        ogg_stream_read(&stream, get32(data, size, 49), out, sizeof out);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    uint8_t control = size == 0 ? 0 : data[size - 1];
    MemoryFile file = {.bytes = data, .size = (uint32_t)size};
    FlacInput readable = input(&file);
    exercise_vorbis(readable, control);
    exercise_opus(readable, data, size, control);
    exercise_invalid(readable, data, size);

    file.failing = true;
    file.fail_at = size == 0 ? 0 : get32(data, size, control % size) % (uint32_t)size;
    OggTrack track;
    vorbis_track_probe(input(&file), &track);
    opus_track_probe(input(&file), &track);
    return 0;
}
