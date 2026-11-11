#include "device/dsd_stream.h"

static void number(uint8_t *bytes, uint64_t value, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

bool dsd_stream_start(DsdStream *stream, FlacInput input, uint32_t start_ms, AudioTrackInfo *info)
{
    DsdTrack track;
    if (!dsd_probe(input, &track)) {
        return false;
    }
    uint32_t decimation = track.rate / DSD_PCM_RATE;
    uint32_t frames = (uint32_t)((track.samples - 1) / decimation + 1);
    uint64_t desired = (uint64_t)start_ms * DSD_PCM_RATE / 1000;
    uint32_t target = desired < frames ? (uint32_t)desired : frames;
    uint32_t preroll = target > DSD_PREROLL ? target - DSD_PREROLL : 0;
    uint64_t first_bit = (uint64_t)preroll * decimation;
    uint64_t first_byte = track.layout == 0 ? first_bit / 8 : (first_bit / 32768) * 4096;
    uint32_t skip = (uint32_t)first_byte * track.channels;
    *stream = (DsdStream){.file = input,
                          .track = track,
                          .first = track.data_offset + skip,
                          .bytes = track.data_bytes - skip};
    uint64_t remaining = track.samples - first_byte * 8;
    number(stream->header, 0x50445344u, 4);
    number(stream->header + 4, track.rate, 4);
    number(stream->header + 8, track.channels, 4);
    number(stream->header + 12, track.layout, 4);
    number(stream->header + 16, remaining, 8);
    number(stream->header + 24, first_byte * 8 / decimation, 4);
    number(stream->header + 28, track.layout == 0 ? 0 : DSD_BLOCK_BYTES, 4);
    *info = (AudioTrackInfo){.stream = {.sample_rate = DSD_PCM_RATE,
                                        .channels = (uint8_t)track.channels,
                                        .bits_per_sample = 24,
                                        .min_block = DSD_OUTPUT_FRAMES,
                                        .max_block = DSD_OUTPUT_FRAMES,
                                        .total_samples = frames},
                             .audio_bytes = track.data_bytes,
                             .codec = AUDIO_CODEC_DSD};
    return true;
}

bool dsd_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    DsdStream *stream = context;
    uint8_t *bytes = buffer;
    uint32_t size = DSD_HEADER_BYTES + stream->bytes;
    if (offset > size || length > size - offset) {
        return false;
    }
    while (offset < DSD_HEADER_BYTES && length != 0) {
        *bytes++ = stream->header[offset++];
        length--;
    }
    return length == 0 ||
           stream->file.read(stream->file.context, stream->first + offset - DSD_HEADER_BYTES, bytes,
                             length);
}
