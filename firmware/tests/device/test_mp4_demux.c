#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/mp4_demux.h"
#include "m4a_fixture_files.h"

#define AAC_ALBUM "m4a_music/Aac Artist/Aac Album/"
#define ALAC_ALBUM "m4a_music/Alac Artist/Alac Album/"

enum {
    MAX_BOX_BYTES = 4096,
    SYNTHETIC_SAMPLES = 10,
    SYNTHETIC_CHUNKS = 4
};

static bool probe_file(const char *path, M4aFile *file, Mp4Track *track)
{
    *file = m4a_file_load(path);
    return file->size > 0 && mp4_probe(m4a_file_input(file), track);
}

static void an_aac_file_with_moov_first_reports_its_format_and_gapless_info(void)
{
    M4aFile file;
    Mp4Track track;
    CHECK(probe_file(AAC_ALBUM "01 Lead.m4a", &file, &track));
    CHECK_EQUAL(MP4_CODEC_AAC, track.codec);
    CHECK_EQUAL(2, track.channels);
    CHECK_EQUAL(44100u, track.sample_rate);
    CHECK_EQUAL(1024u, track.frame_samples);
    CHECK_EQUAL(45124u, track.total_samples);
    CHECK_EQUAL(2112u, track.delay);
    CHECK_EQUAL(45124u - 2112u - 700u, track.valid_samples);
    CHECK_EQUAL(45u, track.sample_count);
    CHECK_EQUAL(2u, track.config_size);
    CHECK_EQUAL(0x12, track.config[0]);
    CHECK_EQUAL(0x10, track.config[1]);
    CHECK(track.audio_bytes > 10000);
    m4a_file_free(&file);
}

static void an_aac_file_with_moov_last_uses_its_edit_list(void)
{
    M4aFile file;
    Mp4Track track;
    CHECK(probe_file(AAC_ALBUM "02 Tail.m4a", &file, &track));
    CHECK_EQUAL(MP4_CODEC_AAC, track.codec);
    CHECK_EQUAL(1024u, track.delay);
    CHECK_EQUAL(44100u, track.valid_samples);
    CHECK(track.sample_count > 40);
    m4a_file_free(&file);
}

static void a_malformed_gapless_tag_falls_back_to_the_edit_list(void)
{
    for (int variant = 0; variant < 5; variant++) {
        M4aFile file = m4a_file_load(AAC_ALBUM "01 Lead.m4a");
        static const char PREFIX[] = " 00000000 00000840";
        uint32_t at = 0;
        while (at + sizeof PREFIX <= file.size &&
               memcmp(file.bytes + at, PREFIX, sizeof PREFIX - 1) != 0) {
            at++;
        }
        CHECK(at + 44 <= file.size);
        if (variant < 3) {
            file.bytes[at + 1] = (uint8_t)"Z@`"[variant];
        } else {
            memset(file.bytes + at + 28, variant == 3 ? '0' : 'F', 16);
        }
        Mp4Track track;
        CHECK(mp4_probe(m4a_file_input(&file), &track));
        CHECK_EQUAL(1024u, track.delay);
        CHECK_EQUAL(44100u, track.valid_samples);
        m4a_file_free(&file);
    }
}

static void a_mono_aac_file_at_48k_is_recognised(void)
{
    M4aFile file;
    Mp4Track track;
    CHECK(probe_file(AAC_ALBUM "03 Mono.m4a", &file, &track));
    CHECK_EQUAL(1, track.channels);
    CHECK_EQUAL(48000u, track.sample_rate);
    m4a_file_free(&file);
}

static void alac_files_report_their_cookie_depth_and_channels(void)
{
    M4aFile file;
    Mp4Track track;
    CHECK(probe_file(ALAC_ALBUM "01 Lossless.m4a", &file, &track));
    CHECK_EQUAL(MP4_CODEC_ALAC, track.codec);
    CHECK_EQUAL(24, track.config_size);
    CHECK_EQUAL(16, track.bits);
    CHECK_EQUAL(2, track.channels);
    CHECK_EQUAL(44100u, track.sample_rate);
    CHECK_EQUAL(4096u, track.frame_samples);
    m4a_file_free(&file);
    CHECK(probe_file(ALAC_ALBUM "02 Deep.m4a", &file, &track));
    CHECK_EQUAL(24, track.bits);
    CHECK_EQUAL(48000u, track.sample_rate);
    m4a_file_free(&file);
    CHECK(probe_file(ALAC_ALBUM "03 Single.m4a", &file, &track));
    CHECK_EQUAL(1, track.channels);
    m4a_file_free(&file);
}

static void the_cursor_visits_every_sample_inside_the_media_data(void)
{
    M4aFile file;
    Mp4Track track;
    CHECK(probe_file(AAC_ALBUM "02 Tail.m4a", &file, &track));
    Mp4Cursor cursor;
    CHECK(mp4_cursor_seek(&cursor, m4a_file_input(&file), &track, 0));
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t previous_end = 0;
    uint32_t total = 0;
    uint32_t count = 0;
    while (mp4_cursor_next(&cursor, m4a_file_input(&file), &track, &offset, &size)) {
        CHECK(offset >= previous_end);
        CHECK(offset + size <= file.size);
        previous_end = offset + size;
        total += size;
        count++;
    }
    CHECK_EQUAL(track.sample_count, count);
    CHECK_EQUAL(track.audio_bytes, total);
    m4a_file_free(&file);
}

static void seeking_lands_on_the_same_sample_as_walking(void)
{
    M4aFile file;
    Mp4Track track;
    CHECK(probe_file(ALAC_ALBUM "01 Lossless.m4a", &file, &track));
    FlacInput input = m4a_file_input(&file);
    uint32_t offsets[64];
    uint32_t sizes[64];
    Mp4Cursor walker;
    CHECK(mp4_cursor_seek(&walker, input, &track, 0));
    for (uint32_t index = 0; index < track.sample_count; index++) {
        CHECK(mp4_cursor_next(&walker, input, &track, &offsets[index], &sizes[index]));
    }
    for (uint32_t index = 0; index < track.sample_count; index++) {
        Mp4Cursor cursor;
        uint32_t offset;
        uint32_t size;
        CHECK(mp4_cursor_seek(&cursor, input, &track, index));
        CHECK(mp4_cursor_next(&cursor, input, &track, &offset, &size));
        CHECK_EQUAL(offsets[index], offset);
        CHECK_EQUAL(sizes[index], size);
    }
    Mp4Cursor past;
    CHECK(!mp4_cursor_seek(&past, input, &track, track.sample_count));
    m4a_file_free(&file);
}

typedef struct Builder {
    uint8_t bytes[MAX_BOX_BYTES];
    uint32_t length;
} Builder;

static void put_byte(Builder *builder, uint32_t value)
{
    builder->bytes[builder->length++] = (uint8_t)value;
}

static void put_word(Builder *builder, uint32_t value)
{
    put_byte(builder, value >> 24);
    put_byte(builder, value >> 16);
    put_byte(builder, value >> 8);
    put_byte(builder, value);
}

static void put_type(Builder *builder, const char *type)
{
    for (int index = 0; index < 4; index++) {
        put_byte(builder, (uint8_t)type[index]);
    }
}

static uint32_t begin_box(Builder *builder, const char *type)
{
    uint32_t start = builder->length;
    put_word(builder, 0);
    put_type(builder, type);
    return start;
}

static void end_box(Builder *builder, uint32_t start)
{
    uint32_t size = builder->length - start;
    builder->bytes[start] = (uint8_t)(size >> 24);
    builder->bytes[start + 1] = (uint8_t)(size >> 16);
    builder->bytes[start + 2] = (uint8_t)(size >> 8);
    builder->bytes[start + 3] = (uint8_t)size;
}

static void put_sample_description(Builder *builder)
{
    uint32_t stsd = begin_box(builder, "stsd");
    put_word(builder, 0);
    put_word(builder, 1);
    uint32_t entry = begin_box(builder, "mp4a");
    for (int index = 0; index < 28; index++) {
        put_byte(builder, index == 17 ? 2 : 0);
    }
    uint32_t esds = begin_box(builder, "esds");
    put_word(builder, 0);
    static const uint8_t DESCRIPTORS[] = {3, 25, 0, 0, 0, 4, 17, 0x40, 0x15, 0,    0, 0, 0, 0,
                                          0, 0,  0, 0, 0, 0, 5,  2,    0x12, 0x10, 6, 1, 2};
    for (size_t index = 0; index < sizeof DESCRIPTORS; index++) {
        put_byte(builder, DESCRIPTORS[index]);
    }
    end_box(builder, esds);
    end_box(builder, entry);
    end_box(builder, stsd);
}

static void put_timing_and_sizes(Builder *builder, uint32_t ticks, uint32_t frames)
{
    uint32_t stts = begin_box(builder, "stts");
    put_word(builder, 0);
    put_word(builder, 1);
    put_word(builder, frames);
    put_word(builder, ticks);
    end_box(builder, stts);
    uint32_t stsz = begin_box(builder, "stsz");
    put_word(builder, 0);
    put_word(builder, 0);
    put_word(builder, frames);
    for (uint32_t index = 0; index < frames; index++) {
        put_word(builder, 100 + index);
    }
    end_box(builder, stsz);
}

static void put_chunks(Builder *builder, bool wide, uint32_t frames)
{
    uint32_t stsc = begin_box(builder, "stsc");
    put_word(builder, 0);
    put_word(builder, frames == SYNTHETIC_SAMPLES ? 2 : 1);
    put_word(builder, 1);
    put_word(builder, frames == SYNTHETIC_SAMPLES ? 3 : frames / SYNTHETIC_CHUNKS);
    put_word(builder, 1);
    if (frames == SYNTHETIC_SAMPLES) {
        put_word(builder, 3);
        put_word(builder, 2);
        put_word(builder, 1);
    }
    end_box(builder, stsc);
    uint32_t stco = begin_box(builder, wide ? "co64" : "stco");
    put_word(builder, 0);
    put_word(builder, SYNTHETIC_CHUNKS);
    static const uint32_t CHUNKS[SYNTHETIC_CHUNKS] = {1000, 5000, 9000, 20000};
    for (int index = 0; index < SYNTHETIC_CHUNKS; index++) {
        if (wide) {
            put_word(builder, 0);
        }
        put_word(builder, CHUNKS[index]);
    }
    end_box(builder, stco);
}

typedef struct EditSpec {
    uint32_t movie_scale;
    uint32_t media_scale;
    uint64_t duration;
    uint64_t media_time;
    uint32_t version;
    uint32_t rate;
    uint32_t entries;
    uint32_t frames;
    bool wide_headers;
    bool truncated;
    bool video_first;
} EditSpec;

static void put_wide(Builder *builder, uint64_t value)
{
    put_word(builder, (uint32_t)(value >> 32));
    put_word(builder, (uint32_t)value);
}

static void put_time_header(Builder *builder, const char *type, uint32_t scale, bool wide)
{
    uint32_t box = begin_box(builder, type);
    put_word(builder, wide ? 0x01000000 : 0);
    if (wide) {
        put_wide(builder, 0);
        put_wide(builder, 0);
    } else {
        put_word(builder, 0);
        put_word(builder, 0);
    }
    put_word(builder, scale);
    end_box(builder, box);
}

static void put_edit(Builder *builder, const EditSpec *edit)
{
    uint32_t edts = begin_box(builder, "edts");
    uint32_t free = begin_box(builder, "free");
    end_box(builder, free);
    uint32_t elst = begin_box(builder, "elst");
    put_word(builder, edit->version << 24);
    put_word(builder, edit->entries);
    for (uint32_t index = 0; index < (edit->entries == 2 ? 2u : 1u); index++) {
        if (edit->version == 1) {
            put_wide(builder, edit->duration);
            put_wide(builder, edit->media_time);
        } else {
            put_word(builder, (uint32_t)edit->duration);
            put_word(builder, (uint32_t)edit->media_time);
        }
        if (!edit->truncated) {
            put_word(builder, edit->rate);
        }
    }
    end_box(builder, elst);
    end_box(builder, edts);
}

static void put_video_track(Builder *builder, const EditSpec *edit)
{
    uint32_t trak = begin_box(builder, "trak");
    put_edit(builder, edit);
    uint32_t mdia = begin_box(builder, "mdia");
    uint32_t hdlr = begin_box(builder, "hdlr");
    put_word(builder, 0);
    put_word(builder, 0);
    put_type(builder, "vide");
    end_box(builder, hdlr);
    end_box(builder, mdia);
    end_box(builder, trak);
}

static void build_with_edit(Builder *builder, bool wide, const EditSpec *edit)
{
    *builder = (Builder){.length = 0};
    uint32_t ftyp = begin_box(builder, "ftyp");
    put_type(builder, "M4A ");
    end_box(builder, ftyp);
    uint32_t moov = begin_box(builder, "moov");
    if (edit != NULL) {
        put_time_header(builder, "mvhd", edit->movie_scale, edit->wide_headers);
        if (edit->video_first) {
            EditSpec video = *edit;
            video.media_time = 9000;
            put_video_track(builder, &video);
        }
    }
    uint32_t trak = begin_box(builder, "trak");
    if (edit != NULL) {
        put_edit(builder, edit);
    }
    uint32_t mdia = begin_box(builder, "mdia");
    if (edit != NULL) {
        put_time_header(builder, "mdhd", edit->media_scale, edit->wide_headers);
    }
    uint32_t hdlr = begin_box(builder, "hdlr");
    put_word(builder, 0);
    put_word(builder, 0);
    put_type(builder, "soun");
    end_box(builder, hdlr);
    uint32_t minf = begin_box(builder, "minf");
    uint32_t stbl = begin_box(builder, "stbl");
    put_sample_description(builder);
    uint32_t scale = edit != NULL && edit->media_scale != 0 ? edit->media_scale : 44100;
    uint32_t frames = edit != NULL && edit->frames != 0 ? edit->frames : SYNTHETIC_SAMPLES;
    put_timing_and_sizes(builder, (uint32_t)((uint64_t)1024 * scale / 44100), frames);
    put_chunks(builder, wide, frames);
    end_box(builder, stbl);
    end_box(builder, minf);
    end_box(builder, mdia);
    end_box(builder, trak);
    end_box(builder, moov);
}

static void build_synthetic(Builder *builder, bool wide)
{
    build_with_edit(builder, wide, NULL);
}

static bool read_builder(void *context, uint32_t offset, void *destination, uint32_t length)
{
    const Builder *builder = context;
    if (offset > builder->length || length > builder->length - offset) {
        return false;
    }
    memcpy(destination, builder->bytes + offset, length);
    return true;
}

typedef struct FailedInput {
    Builder *builder;
    uint32_t calls;
    uint32_t fail_at;
} FailedInput;

static bool read_with_failure(void *context, uint32_t offset, void *destination, uint32_t length)
{
    FailedInput *input = context;
    input->calls++;
    return input->calls != input->fail_at &&
           read_builder(input->builder, offset, destination, length);
}

static void read_failures_never_publish_a_different_track_layout(void)
{
    static Builder builder;
    build_synthetic(&builder, false);
    FailedInput failure = {.builder = &builder};
    FlacInput input = {.read = read_with_failure, .context = &failure, .size = builder.length};
    Mp4Track expected;
    CHECK(mp4_probe(input, &expected));
    uint32_t reads = failure.calls;
    CHECK(reads > 0);
    for (uint32_t at = 1; at <= reads; at++) {
        failure.calls = 0;
        failure.fail_at = at;
        Mp4Track track;
        if (mp4_probe(input, &track)) {
            CHECK_EQUAL(expected.codec, track.codec);
            CHECK_EQUAL(expected.channels, track.channels);
            CHECK_EQUAL(expected.sample_rate, track.sample_rate);
            CHECK_EQUAL(expected.sample_count, track.sample_count);
            CHECK_EQUAL(expected.total_samples, track.total_samples);
        }
    }
}

static void seek_read_failures_are_reported_and_a_retry_can_recover(void)
{
    static Builder builder;
    build_synthetic(&builder, true);
    FailedInput failure = {.builder = &builder};
    FlacInput input = {.read = read_with_failure, .context = &failure, .size = builder.length};
    Mp4Track track;
    Mp4Cursor cursor;
    CHECK(mp4_probe(input, &track));
    failure.calls = 0;
    CHECK(mp4_cursor_seek(&cursor, input, &track, 5));
    uint32_t reads = failure.calls;
    CHECK(reads > 0);
    for (uint32_t at = 1; at <= reads; at++) {
        failure.calls = 0;
        failure.fail_at = at;
        CHECK(!mp4_cursor_seek(&cursor, input, &track, 5));
        failure.calls = 0;
        failure.fail_at = 0;
        CHECK(mp4_cursor_seek(&cursor, input, &track, 5));
        uint32_t offset;
        uint32_t size;
        CHECK(mp4_cursor_next(&cursor, input, &track, &offset, &size));
        CHECK_EQUAL(5207u, offset);
        CHECK_EQUAL(105u, size);
    }
}

static void a_chunk_transition_read_failure_stops_after_the_current_sample(void)
{
    static Builder builder;
    build_synthetic(&builder, false);
    FailedInput failure = {.builder = &builder};
    FlacInput input = {.read = read_with_failure, .context = &failure, .size = builder.length};
    Mp4Track track;
    Mp4Cursor cursor;
    CHECK(mp4_probe(input, &track));
    CHECK(mp4_cursor_seek(&cursor, input, &track, 2));
    failure.calls = 0;
    failure.fail_at = 1;
    uint32_t offset;
    uint32_t size;
    CHECK(mp4_cursor_next(&cursor, input, &track, &offset, &size));
    CHECK_EQUAL(1201u, offset);
    CHECK_EQUAL(102u, size);
    CHECK(!mp4_cursor_next(&cursor, input, &track, &offset, &size));
    failure.fail_at = 0;
    CHECK(mp4_cursor_seek(&cursor, input, &track, 3));
    CHECK(mp4_cursor_next(&cursor, input, &track, &offset, &size));
    CHECK_EQUAL(5000u, offset);
    CHECK_EQUAL(103u, size);
}

static Mp4Track probe_edit(const EditSpec *edit)
{
    static Builder builder;
    build_with_edit(&builder, false, edit);
    Mp4Track track;
    CHECK(mp4_probe((FlacInput){read_builder, &builder, builder.length}, &track));
    return track;
}

static EditSpec simple_edit(void)
{
    return (EditSpec){.movie_scale = 1000,
                      .media_scale = 88200,
                      .duration = 100,
                      .media_time = 2048,
                      .rate = 0x00010000,
                      .entries = 1};
}

static void edit_duration_and_start_use_their_own_time_scales(void)
{
    EditSpec edit = simple_edit();
    Mp4Track track = probe_edit(&edit);
    CHECK_EQUAL(1024u, track.delay);
    CHECK_EQUAL(4410u, track.valid_samples);
    CHECK_EQUAL(10240u, track.total_samples);
    CHECK_EQUAL(1024u, track.frame_samples);
}

static void coarse_media_clocks_keep_the_decoder_frame_boundaries(void)
{
    EditSpec edit = simple_edit();
    edit.media_scale = 1000;
    edit.media_time = 23;
    Mp4Track track = probe_edit(&edit);
    CHECK_EQUAL(1024u, track.frame_samples);
    CHECK_EQUAL(1014u, track.delay);
    CHECK_EQUAL(4410u, track.valid_samples);
}

static void a_rounded_edit_duration_is_clamped_to_available_audio(void)
{
    EditSpec edit = simple_edit();
    edit.duration = 210;
    Mp4Track track = probe_edit(&edit);
    CHECK_EQUAL(1024u, track.delay);
    CHECK_EQUAL(9216u, track.valid_samples);
}

static void version_one_edits_and_headers_keep_wide_durations(void)
{
    EditSpec edit = simple_edit();
    edit.version = 1;
    edit.wide_headers = true;
    edit.movie_scale = 4000000000u;
    edit.duration = 5000000000ull;
    edit.frames = 100;
    Mp4Track track = probe_edit(&edit);
    CHECK_EQUAL(1024u, track.delay);
    CHECK_EQUAL(55125u, track.valid_samples);
    CHECK_EQUAL(102400u, track.total_samples);
}

static void an_unrelated_tracks_edit_never_trims_the_audio(void)
{
    EditSpec edit = simple_edit();
    edit.video_first = true;
    Mp4Track track = probe_edit(&edit);
    CHECK_EQUAL(1024u, track.delay);
    CHECK_EQUAL(4410u, track.valid_samples);
}

static void malformed_and_non_linear_edits_do_not_guess_a_trim(void)
{
    for (int index = 0; index < 12; index++) {
        EditSpec edit = simple_edit();
        switch (index) {
        case 0:
            edit.version = 2;
            break;
        case 1:
            edit.media_time = UINT32_MAX;
            break;
        case 2:
            edit.rate = 0;
            break;
        case 3:
            edit.rate = 0x00020000;
            break;
        case 4:
            edit.entries = 2;
            break;
        case 5:
            edit.movie_scale = 0;
            break;
        case 6:
            edit.media_scale = 0;
            break;
        case 7:
            edit.media_time = 999999;
            break;
        case 8:
            edit.version = 1;
            edit.duration = UINT64_MAX;
            break;
        case 9:
            edit.version = 1;
            edit.media_time = UINT64_MAX;
            break;
        case 10:
            edit.truncated = true;
            break;
        case 11:
            edit.duration = 0;
            break;
        }
        Mp4Track track = probe_edit(&edit);
        CHECK_EQUAL(0u, track.delay);
        CHECK_EQUAL(track.total_samples, track.valid_samples);
    }
}

static void overflowing_or_truncated_timing_is_refused(void)
{
    for (int variant = 0; variant < 2; variant++) {
        static Builder builder;
        build_synthetic(&builder, false);
        uint32_t at = 0;
        while (at + 20 <= builder.length && memcmp(builder.bytes + at, "stts", 4) != 0) {
            at++;
        }
        CHECK(at + 20 <= builder.length);
        if (variant == 0) {
            memset(builder.bytes + at + 12, 255, 4);
        } else {
            builder.bytes[at + 11] = 65;
        }
        Mp4Track track;
        CHECK(!mp4_probe((FlacInput){read_builder, &builder, builder.length}, &track));
    }
}

static void check_synthetic_layout(bool wide)
{
    static Builder builder;
    build_synthetic(&builder, wide);
    FlacInput input = {.read = read_builder, .context = &builder, .size = builder.length};
    Mp4Track track;
    CHECK(mp4_probe(input, &track));
    CHECK_EQUAL(SYNTHETIC_SAMPLES, track.sample_count);
    CHECK_EQUAL(wide, track.wide_offsets);
    static const uint32_t FIRST_IN_CHUNK[SYNTHETIC_CHUNKS] = {0, 3, 6, 8};
    static const uint32_t CHUNK_OFFSET[SYNTHETIC_CHUNKS] = {1000, 5000, 9000, 20000};
    for (uint32_t start = 0; start < SYNTHETIC_SAMPLES; start++) {
        Mp4Cursor cursor;
        CHECK(mp4_cursor_seek(&cursor, input, &track, start));
        for (uint32_t sample = start; sample < SYNTHETIC_SAMPLES; sample++) {
            uint32_t chunk = 0;
            while (chunk + 1 < SYNTHETIC_CHUNKS && FIRST_IN_CHUNK[chunk + 1] <= sample) {
                chunk++;
            }
            uint32_t expected = CHUNK_OFFSET[chunk];
            for (uint32_t before = FIRST_IN_CHUNK[chunk]; before < sample; before++) {
                expected += 100 + before;
            }
            uint32_t offset;
            uint32_t size;
            CHECK(mp4_cursor_next(&cursor, input, &track, &offset, &size));
            CHECK_EQUAL(expected, offset);
            CHECK_EQUAL(100 + sample, size);
        }
        uint32_t offset;
        uint32_t size;
        CHECK(!mp4_cursor_next(&cursor, input, &track, &offset, &size));
    }
}

static void chunk_runs_with_different_sample_counts_are_walked_and_seeked_exactly(void)
{
    check_synthetic_layout(false);
}

static void sixty_four_bit_chunk_offsets_work_like_narrow_ones(void)
{
    check_synthetic_layout(true);
}

static void a_high_efficiency_configuration_is_refused(void)
{
    M4aFile file = m4a_file_load(AAC_ALBUM "01 Lead.m4a");
    Mp4Track track;
    uint32_t at = 0;
    while (at + 1 < file.size && !(file.bytes[at] == 0x12 && file.bytes[at + 1] == 0x10)) {
        at++;
    }
    CHECK(at + 1 < file.size);
    file.bytes[at] = 0x2A;
    CHECK(!mp4_probe(m4a_file_input(&file), &track));
    m4a_file_free(&file);
}

static void an_sbr_signal_is_refused_only_when_sbr_is_present(void)
{
    M4aFile file = m4a_file_load(AAC_ALBUM "01 Lead.m4a");
    Mp4Track track;
    uint32_t at = 0;
    while (at + 4 < file.size && !(file.bytes[at] == 0x12 && file.bytes[at + 1] == 0x10)) {
        at++;
    }
    CHECK(mp4_probe(m4a_file_input(&file), &track));
    CHECK_EQUAL(0x56, file.bytes[at + 2]);
    file.bytes[at + 4] = 0x80;
    CHECK(!mp4_probe(m4a_file_input(&file), &track));
    m4a_file_free(&file);
}

static void files_that_are_not_mp4_or_are_cut_short_are_refused(void)
{
    M4aFile file = m4a_file_load(AAC_ALBUM "02 Tail.m4a");
    Mp4Track track;
    FlacInput input = m4a_file_input(&file);
    CHECK(mp4_probe(input, &track));
    input.size = file.size - 100;
    CHECK(!mp4_probe(input, &track));
    input.size = 12;
    CHECK(!mp4_probe(input, &track));
    memcpy(file.bytes + 4, "RIFF", 4);
    CHECK(!mp4_signature(file.bytes));
    m4a_file_free(&file);
}

static void the_signature_is_the_ftyp_box(void)
{
    M4aFile file = m4a_file_load(AAC_ALBUM "01 Lead.m4a");
    CHECK(mp4_signature(file.bytes));
    m4a_file_free(&file);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_aac_file_with_moov_first_reports_its_format_and_gapless_info),
        TEST_CASE(an_aac_file_with_moov_last_uses_its_edit_list),
        TEST_CASE(a_malformed_gapless_tag_falls_back_to_the_edit_list),
        TEST_CASE(a_mono_aac_file_at_48k_is_recognised),
        TEST_CASE(alac_files_report_their_cookie_depth_and_channels),
        TEST_CASE(the_cursor_visits_every_sample_inside_the_media_data),
        TEST_CASE(seeking_lands_on_the_same_sample_as_walking),
        TEST_CASE(overflowing_or_truncated_timing_is_refused),
        TEST_CASE(edit_duration_and_start_use_their_own_time_scales),
        TEST_CASE(coarse_media_clocks_keep_the_decoder_frame_boundaries),
        TEST_CASE(a_rounded_edit_duration_is_clamped_to_available_audio),
        TEST_CASE(version_one_edits_and_headers_keep_wide_durations),
        TEST_CASE(an_unrelated_tracks_edit_never_trims_the_audio),
        TEST_CASE(malformed_and_non_linear_edits_do_not_guess_a_trim),
        TEST_CASE(chunk_runs_with_different_sample_counts_are_walked_and_seeked_exactly),
        TEST_CASE(sixty_four_bit_chunk_offsets_work_like_narrow_ones),
        TEST_CASE(read_failures_never_publish_a_different_track_layout),
        TEST_CASE(seek_read_failures_are_reported_and_a_retry_can_recover),
        TEST_CASE(a_chunk_transition_read_failure_stops_after_the_current_sample),
        TEST_CASE(a_high_efficiency_configuration_is_refused),
        TEST_CASE(an_sbr_signal_is_refused_only_when_sbr_is_present),
        TEST_CASE(files_that_are_not_mp4_or_are_cut_short_are_refused),
        TEST_CASE(the_signature_is_the_ftyp_box),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
