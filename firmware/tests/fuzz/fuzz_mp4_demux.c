#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/mp4_demux.h"
#include "fuzz_source.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static FlacInput mp4_input(FuzzSource *source)
{
    ByteSource bytes = fuzz_source(source);
    return (FlacInput){.read = bytes.read, .context = bytes.context, .size = source->size};
}

static bool same_track(const Mp4Track *first, const Mp4Track *second)
{
    return first->codec == second->codec && first->channels == second->channels &&
           first->bits == second->bits && first->config_size == second->config_size &&
           memcmp(first->config, second->config, sizeof first->config) == 0 &&
           first->sample_rate == second->sample_rate &&
           first->frame_samples == second->frame_samples &&
           first->total_samples == second->total_samples && first->delay == second->delay &&
           first->valid_samples == second->valid_samples &&
           first->sample_count == second->sample_count &&
           first->audio_bytes == second->audio_bytes && first->fixed_size == second->fixed_size &&
           first->sizes_at == second->sizes_at && first->chunks_at == second->chunks_at &&
           first->chunk_count == second->chunk_count && first->runs_at == second->runs_at &&
           first->run_count == second->run_count && first->wide_offsets == second->wide_offsets;
}

static uint32_t selected_sample(const uint8_t *data, uint32_t size, uint32_t count)
{
    uint32_t value = size;
    for (uint32_t index = 0; index < 4 && index < size; index++) {
        value = value << 5 ^ data[size - 1 - index];
    }
    return count == 0 ? 0 : value % count;
}

static void check_track(const Mp4Track *track)
{
    fuzz_require(track->codec == MP4_CODEC_AAC || track->codec == MP4_CODEC_ALAC);
    fuzz_require(track->config_size <= MP4_CONFIG_BYTES);
    fuzz_require(track->delay <= track->total_samples);
    fuzz_require(track->valid_samples <= track->total_samples - track->delay);
}

static void check_cursor(FuzzSource *first_source, FuzzSource *second_source, const Mp4Track *track,
                         uint32_t sample)
{
    FlacInput first_input = mp4_input(first_source);
    FlacInput second_input = mp4_input(second_source);
    Mp4Cursor first;
    Mp4Cursor second;
    bool first_seek = mp4_cursor_seek(&first, first_input, track, sample);
    bool second_seek = mp4_cursor_seek(&second, second_input, track, sample);
    fuzz_require(first_seek == second_seek);
    if (!first_seek) {
        return;
    }
    uint32_t limit = track->sample_count - sample;
    limit = limit < 128 ? limit : 128;
    for (uint32_t index = 0; index < limit; index++) {
        uint32_t left_offset = 0;
        uint32_t left_size = 0;
        uint32_t right_offset = 0;
        uint32_t right_size = 0;
        uint32_t before = first.sample;
        bool left = mp4_cursor_next(&first, first_input, track, &left_offset, &left_size);
        bool right = mp4_cursor_next(&second, second_input, track, &right_offset, &right_size);
        fuzz_require(left == right);
        if (!left) {
            break;
        }
        fuzz_require(left_offset == right_offset && left_size == right_size);
        if (track->audio_bytes > 0) {
            fuzz_require(left_offset <= first_input.size &&
                         left_size <= first_input.size - left_offset);
        }
        fuzz_require((first.sample == before + 1 || first.sample == track->sample_count) &&
                     second.sample == first.sample);
    }
}

static bool read_one(FlacInput input, const Mp4Track *track, uint32_t sample, uint32_t *values)
{
    Mp4Cursor cursor;
    return mp4_cursor_seek(&cursor, input, track, sample) &&
           mp4_cursor_next(&cursor, input, track, values, values + 1);
}

static void check_seek_matches_walk(FuzzSource *source, const Mp4Track *track, uint32_t sample)
{
    if (sample >= 128) {
        return;
    }
    FlacInput input = mp4_input(source);
    uint32_t direct[2];
    if (!read_one(input, track, sample, direct)) {
        return;
    }
    Mp4Cursor cursor;
    if (!mp4_cursor_seek(&cursor, input, track, 0)) {
        return;
    }
    uint32_t walked[2] = {0};
    for (uint32_t index = 0; index <= sample; index++) {
        if (!mp4_cursor_next(&cursor, input, track, walked, walked + 1)) {
            return;
        }
    }
    fuzz_require(direct[0] == walked[0] && direct[1] == walked[1]);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    FuzzSource first_source = {.bytes = data, .size = (uint32_t)size};
    FuzzSource second_source = first_source;
    Mp4Track first;
    Mp4Track second;
    bool first_result = mp4_probe(mp4_input(&first_source), &first);
    bool second_result = mp4_probe(mp4_input(&second_source), &second);
    fuzz_require(first_result == second_result);
    fuzz_require(!first_source.invalid_read && !second_source.invalid_read);
    if (!first_result) {
        return 0;
    }
    fuzz_require(same_track(&first, &second));
    check_track(&first);
    uint32_t sample = selected_sample(data, (uint32_t)size, first.sample_count);
    if (first.sample_count > 0) {
        check_cursor(&first_source, &second_source, &first, sample);
        check_seek_matches_walk(&first_source, &first, sample);
    }
    Mp4Cursor past;
    fuzz_require(!mp4_cursor_seek(&past, mp4_input(&first_source), &first, first.sample_count));
    fuzz_require(!first_source.invalid_read && !second_source.invalid_read);
    return 0;
}
