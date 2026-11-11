#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "library/track_tags.h"
#include "memory_source.h"

static const char TITLE[] = "Svefn-g-englar \xe2\x9c\x93";
static const char ARTIST[] = "Sigur R\xc3\xb3s";
static const char ALBUM_ARTIST[] = "Sigur R\xc3\xb3s & Friends";
static const char ALBUM[] = "\xc3\x81g\xc3\xa6tis byrjun";

typedef struct Expected {
    const char *name;
    TagFormat format;
    const char *genre;
    uint32_t duration_ms;
    TagPictureKind picture;
} Expected;

static const Expected TAGGED[] = {
    {"tagged.flac", TAG_FORMAT_FLAC, "Post-Rock", 1000, TAG_PICTURE_RAW},
    {"tagged.ogg", TAG_FORMAT_OGG, "Post-Rock", 1001, TAG_PICTURE_BASE64_FLAC_BLOCK},
    {"id3v24.mp3", TAG_FORMAT_MP3, "Post-Rock", 1045, TAG_PICTURE_RAW},
    {"id3v23_vbr.mp3", TAG_FORMAT_MP3, "Rock", 1032, TAG_PICTURE_RAW},
    {"aac.m4a", TAG_FORMAT_MP4, "Post-Rock", 1023, TAG_PICTURE_RAW},
    {"alac.m4a", TAG_FORMAT_MP4, "Post-Rock", 1000, TAG_PICTURE_RAW},
    {"tagged.wav", TAG_FORMAT_WAV, "Post-Rock", 1000, TAG_PICTURE_RAW},
    {"tagged.wma", TAG_FORMAT_WMA, "Post-Rock", 1000, TAG_PICTURE_RAW},
    {"tagged.ape", TAG_FORMAT_APE, "Post-Rock", 3442, TAG_PICTURE_RAW},
    {"tagged.dsf", TAG_FORMAT_DSF, "Post-Rock", 100, TAG_PICTURE_RAW},
    {"tagged.dff", TAG_FORMAT_DFF, "Post-Rock", 100, TAG_PICTURE_RAW},
};

typedef struct Fixture {
    uint8_t *bytes;
    uint32_t size;
    MemorySource memory;
} Fixture;

static bool load(Fixture *fixture, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/tags/%s", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    fixture->size = (uint32_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    fixture->bytes = malloc(fixture->size);
    bool read = fread(fixture->bytes, 1, fixture->size, file) == fixture->size;
    fclose(file);
    fixture->memory = (MemorySource){.bytes = fixture->bytes, .size = fixture->size};
    return read;
}

static bool read_fixture(const char *name, TrackTags *tags)
{
    Fixture fixture;
    CHECK(load(&fixture, name));
    bool read = track_tags_read(memory_source(&fixture.memory), fixture.size,
                                track_tags_format_for_name(name), tags);
    free(fixture.bytes);
    return read;
}

static int leading_number(const char *text)
{
    return atoi(text);
}

static void check_picture(const char *name, const TrackTags *tags, TagPictureKind kind)
{
    Fixture fixture;
    CHECK(load(&fixture, name));
    CHECK_EQUAL(kind, tags->picture.kind);
    CHECK(tags->picture.front);
    CHECK(tags->picture.offset + tags->picture.length <= fixture.size);
    if (kind == TAG_PICTURE_RAW) {
        const uint8_t *start = fixture.bytes + tags->picture.offset;
        CHECK(start[0] == 0xFF && start[1] == 0xD8);
        CHECK(start[tags->picture.length - 2] == 0xFF && start[tags->picture.length - 1] == 0xD9);
    }
    free(fixture.bytes);
}

static void every_tagged_format_reads_its_fields_duration_and_cover(void)
{
    for (size_t index = 0; index < sizeof TAGGED / sizeof TAGGED[0]; index++) {
        const Expected *expected = &TAGGED[index];
        static TrackTags tags;
        CHECK_EQUAL(expected->format, track_tags_format_for_name(expected->name));
        CHECK(read_fixture(expected->name, &tags));
        CHECK_EQUAL(0, strcmp(TITLE, tags.text[TAG_TITLE]));
        CHECK_EQUAL(0, strcmp(ARTIST, tags.text[TAG_ARTIST]));
        CHECK_EQUAL(0, strcmp(ALBUM_ARTIST, tags.text[TAG_ALBUM_ARTIST]));
        CHECK_EQUAL(0, strcmp(ALBUM, tags.text[TAG_ALBUM]));
        CHECK_EQUAL(0, strcmp(expected->genre, tags.text[TAG_GENRE]));
        CHECK_EQUAL(1999, leading_number(tags.text[TAG_DATE]));
        CHECK_EQUAL(1, leading_number(tags.text[TAG_DISC]));
        CHECK_EQUAL(4, leading_number(tags.text[TAG_TRACK]));
        CHECK_EQUAL(expected->duration_ms, tags.duration_ms);
        check_picture(expected->name, &tags, expected->picture);
    }
}

static void an_mp3_with_only_id3v1_and_no_xing_header_still_reads(void)
{
    static TrackTags tags;
    CHECK(read_fixture("id3v1_no_xing.mp3", &tags));
    CHECK_EQUAL(0, strcmp("Old Song", tags.text[TAG_TITLE]));
    CHECK_EQUAL(0, strcmp("Old Artist", tags.text[TAG_ARTIST]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_ALBUM_ARTIST]));
    CHECK_EQUAL(0, strcmp("Old Album", tags.text[TAG_ALBUM]));
    CHECK_EQUAL(0, strcmp("Rock", tags.text[TAG_GENRE]));
    CHECK_EQUAL(0, strcmp("1987", tags.text[TAG_DATE]));
    CHECK_EQUAL(7, leading_number(tags.text[TAG_TRACK]));
    CHECK_EQUAL(947u, tags.duration_ms);
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
}

static void an_untagged_flac_has_empty_fields_and_its_duration(void)
{
    static TrackTags tags;
    CHECK(read_fixture("untagged.flac", &tags));
    for (int field = 0; field < TAG_FIELD_COUNT; field++) {
        CHECK_EQUAL(0, strcmp("", tags.text[field]));
    }
    CHECK_EQUAL(1000u, tags.duration_ms);
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
}

static void formats_come_from_the_extension_in_any_case(void)
{
    CHECK_EQUAL(TAG_FORMAT_FLAC, track_tags_format_for_name("a.FLAC"));
    CHECK_EQUAL(TAG_FORMAT_MP4, track_tags_format_for_name("song.M4a"));
    CHECK_EQUAL(TAG_FORMAT_MP4, track_tags_format_for_name("Book.m4b"));
    CHECK_EQUAL(TAG_FORMAT_UNKNOWN, track_tags_format_for_name("cover.jpg"));
    CHECK_EQUAL(TAG_FORMAT_UNKNOWN, track_tags_format_for_name("flac"));
}

static void a_truncated_file_fails_cleanly(void)
{
    static TrackTags tags;
    Fixture fixture;
    CHECK(load(&fixture, "tagged.flac"));
    fixture.memory.size = 40;
    CHECK(!track_tags_read(memory_source(&fixture.memory), 40, TAG_FORMAT_FLAC, &tags));
    free(fixture.bytes);
}

static const char *const ALL_FIXTURES[] = {
    "tagged.flac", "tagged.ogg", "id3v24.mp3",    "id3v23_vbr.mp3", "id3v1_no_xing.mp3",
    "aac.m4a",     "alac.m4a",   "tagged.wav",    "tagged.wma",     "tagged.ape",
    "tagged.dsf",  "tagged.dff", "untagged.flac",
};

static void damaged_files_never_read_outside_the_file(void)
{
    static TrackTags tags;
    for (size_t index = 0; index < sizeof ALL_FIXTURES / sizeof ALL_FIXTURES[0]; index++) {
        const char *name = ALL_FIXTURES[index];
        Fixture fixture;
        CHECK(load(&fixture, name));
        uint8_t *copy = malloc(fixture.size);
        for (uint32_t cut = 0; cut < fixture.size; cut += 1 + fixture.size / 300) {
            MemorySource truncated = {.bytes = fixture.bytes, .size = cut};
            track_tags_read(memory_source(&truncated), cut, track_tags_format_for_name(name),
                            &tags);
        }
        for (uint32_t flip = 0; flip < fixture.size; flip += 1 + fixture.size / 300) {
            memcpy(copy, fixture.bytes, fixture.size);
            copy[flip] ^= 0xFF;
            if (flip + 1 < fixture.size) {
                copy[flip + 1] = 0xFF;
            }
            MemorySource damaged = {.bytes = copy, .size = fixture.size};
            track_tags_read(memory_source(&damaged), fixture.size, track_tags_format_for_name(name),
                            &tags);
            for (int field = 0; field < TAG_FIELD_COUNT; field++) {
                CHECK(memchr(tags.text[field], '\0', TRACK_TAG_TEXT_CAPACITY) != NULL);
            }
            CHECK(tags.picture.kind == TAG_PICTURE_NONE ||
                  tags.picture.offset + tags.picture.length <= fixture.size ||
                  tags.picture.kind == TAG_PICTURE_BASE64_FLAC_BLOCK);
        }
        free(copy);
        free(fixture.bytes);
    }
}

static bool read_id3_frame_with_header(uint8_t version, uint8_t flags, uint8_t tag_flags,
                                       const char *id, const uint8_t *data, uint32_t length,
                                       TrackTags *tags)
{
    Fixture audio;
    uint32_t extra = (tag_flags & 0x40) != 0 ? 6 : 0;
    uint32_t header_size = version == 2 ? 6 : 10;
    if (header_size + extra + length >= 128 || !load(&audio, "id3v1_no_xing.mp3")) {
        return false;
    }
    uint32_t total = 10 + extra + header_size + length + audio.size;
    uint8_t *bytes = calloc(total, 1);
    if (bytes == NULL) {
        free(audio.bytes);
        return false;
    }
    memcpy(bytes, "ID3", 3);
    bytes[3] = version;
    bytes[5] = tag_flags;
    bytes[9] = (uint8_t)(extra + header_size + length);
    if (extra != 0) {
        bytes[13] = version == 4 ? 6 : 2;
    }
    uint32_t frame = 10 + extra;
    memcpy(bytes + frame, id, version == 2 ? 3 : 4);
    if (version == 2) {
        bytes[frame + 5] = (uint8_t)length;
    } else {
        bytes[frame + 7] = (uint8_t)length;
        bytes[frame + 9] = flags;
    }
    memcpy(bytes + frame + header_size, data, length);
    memcpy(bytes + frame + header_size + length, audio.bytes, audio.size);
    MemorySource memory = {.bytes = bytes, .size = total};
    bool ok = track_tags_read(memory_source(&memory), total, TAG_FORMAT_MP3, tags);
    free(bytes);
    free(audio.bytes);
    return ok;
}

static bool read_id3_frame(uint8_t version, uint8_t flags, const char *id, const uint8_t *data,
                           uint32_t length, TrackTags *tags)
{
    return read_id3_frame_with_header(version, flags, 0, id, data, length, tags);
}

static void unicode_tags_trim_spaces_without_losing_characters(void)
{
    const struct {
        uint8_t data[16];
        uint32_t length;
        const char *expected;
    } cases[] = {
        {{3, ' ', 'A', 0xe3, 0x80, 0x80}, 6, "A"},
        {{3, 'A', 0xe9, 0xbe, 0x8d}, 5, "A\xe9\xbe\x8d"},
        {{3, 'A', 0xe3, 0x80, 0x80, 'B'},
         6,
         "A\xe3\x80\x80"
         "B"},
        {{2, 0, 'A', 0x30, 0}, 5, "A"},
        {{0, 'A', 0xa0}, 3, "A"},
        {{1, 0xff, 0xfe, 'A', 0, 0x8d, 0x9f}, 7, "A\xe9\xbe\x8d"},
        {{1, 0xfe, 0xff, 0x9f, 0x8d}, 5, "\xe9\xbe\x8d"},
        {{1, 0xfe, 0x00}, 3, "\xc3\xbe"},
        {{1, 0xff, 0x00}, 3, "\xc3\xbf"},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        TrackTags tags;
        CHECK(read_id3_frame(4, 0, "TIT2", cases[index].data, cases[index].length, &tags));
        CHECK_EQUAL(0, strcmp(cases[index].expected, tags.text[TAG_TITLE]));
    }
}

static void id3_genres_handle_numeric_special_and_escaped_values(void)
{
    const struct {
        const char *value;
        const char *expected;
    } cases[] = {
        {"17", "Rock"},
        {"(17)Rock", "Rock"},
        {"CR", "Cover"},
        {"(RX)", "Remix"},
        {"((Live", "(Live"},
        {"Unlisted", "Unlisted"},
        {"(bad)Label", "(bad)Label"},
        {"RX", "Remix"},
        {"(CR)", "Cover"},
        {"256", "256"},
        {"1", "Classic Rock"},
        {"(", "("},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        uint8_t data[64] = {3};
        size_t length = strlen(cases[index].value);
        memcpy(data + 1, cases[index].value, length);
        TrackTags tags;
        CHECK(read_id3_frame(4, 0, "TCON", data, (uint32_t)length + 1, &tags));
        CHECK_EQUAL(0, strcmp(cases[index].expected, tags.text[TAG_GENRE]));
    }
}

static void id3_frame_versions_and_flags_preserve_usable_titles(void)
{
    static const uint8_t plain[] = {3, 'N', 'e', 'w'};
    static const uint8_t grouped[] = {7, 3, 'N', 'e', 'w'};
    static const uint8_t with_length[] = {0, 0, 0, 4, 3, 'N', 'e', 'w'};
    TrackTags tags;
    CHECK(read_id3_frame(2, 0, "TT2", plain, sizeof plain, &tags));
    CHECK_EQUAL(0, strcmp("New", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(3, 0x20, "TIT2", grouped, sizeof grouped, &tags));
    CHECK_EQUAL(0, strcmp("New", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(4, 0x01, "TIT2", with_length, sizeof with_length, &tags));
    CHECK_EQUAL(0, strcmp("New", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(4, 0x08, "TIT2", plain, sizeof plain, &tags));
    CHECK_EQUAL(0, strcmp("Old Song", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(3, 0x80, "TIT2", plain, sizeof plain, &tags));
    CHECK_EQUAL(0, strcmp("Old Song", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(4, 0x40, "TIT2", grouped, sizeof grouped, &tags));
    CHECK_EQUAL(0, strcmp("New", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(4, 0x40, "TIT2", plain, 0, &tags));
    CHECK_EQUAL(0, strcmp("Old Song", tags.text[TAG_TITLE]));
}

static void id3_unsynchronised_text_restores_escaped_bytes(void)
{
    static const uint8_t data[] = {0, 'A', 0xff, 0, 0xe1};
    TrackTags tags;
    CHECK(read_id3_frame(4, 0x02, "TIT2", data, sizeof data, &tags));
    CHECK_EQUAL(0, strcmp("A\xc3\xbf\xc3\xa1", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame_with_header(4, 0, 0x80, "TIT2", data, sizeof data, &tags));
    CHECK_EQUAL(0, strcmp("A\xc3\xbf\xc3\xa1", tags.text[TAG_TITLE]));
    static const uint8_t unescaped[] = {0, 0xff, 0xe1, 0xff};
    CHECK(read_id3_frame(4, 0x02, "TIT2", unescaped, sizeof unescaped, &tags));
    CHECK_EQUAL(0, strcmp("\xc3\xbf\xc3\xa1\xc3\xbf", tags.text[TAG_TITLE]));
}

static void id3_picture_frames_accept_old_and_new_cover_layouts(void)
{
    static const uint8_t modern[] = {0,   'i', 'm', 'a', 'g', 'e',  '/',  'j',  'p',
                                     'e', 'g', 0,   3,   0,   0xff, 0xd8, 0xff, 0xd9};
    static const uint8_t old[] = {0, 'J', 'P', 'G', 3, 0, 0xff, 0xd8, 0xff, 0xd9};
    static const uint8_t back[] = {0, 'i', 0, 4, 0, 0xff, 0xd8};
    TrackTags tags;
    CHECK(read_id3_frame(4, 0, "APIC", modern, sizeof modern, &tags));
    CHECK_EQUAL(TAG_PICTURE_RAW, tags.picture.kind);
    CHECK(tags.picture.front);
    CHECK_EQUAL(4, tags.picture.length);
    CHECK(read_id3_frame(2, 0, "PIC", old, sizeof old, &tags));
    CHECK_EQUAL(TAG_PICTURE_RAW, tags.picture.kind);
    CHECK(tags.picture.front);
    CHECK_EQUAL(4, tags.picture.length);
    CHECK(read_id3_frame(3, 0, "APIC", back, sizeof back, &tags));
    CHECK_EQUAL(TAG_PICTURE_RAW, tags.picture.kind);
    CHECK(!tags.picture.front);
    CHECK_EQUAL(2, tags.picture.length);
    CHECK(read_id3_frame(4, 0x02, "APIC", modern, sizeof modern, &tags));
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
    static const uint8_t no_description[] = {0, 'i', 0, 3, 'x'};
    CHECK(read_id3_frame(4, 0, "APIC", no_description, sizeof no_description, &tags));
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
    static const uint8_t no_type[] = {0, 'i', 0};
    CHECK(read_id3_frame(4, 0, "APIC", no_type, sizeof no_type, &tags));
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
}

static void id3v1_track_numbers_and_missing_genres_are_read_safely(void)
{
    const struct {
        uint8_t track;
        uint8_t comment_end;
        uint8_t genre;
        const char *expected_track;
        const char *expected_genre;
    } cases[] = {
        {0, 0, 255, "", ""},     {12, 0, 17, "12", "Rock"},  {123, 0, 17, "123", "Rock"},
        {' ', 'x', 255, "", ""}, {' ', 0, 17, "32", "Rock"},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        Fixture fixture;
        CHECK(load(&fixture, "id3v1_no_xing.mp3"));
        uint8_t *tag = fixture.bytes + fixture.size - 128;
        tag[125] = cases[index].comment_end;
        tag[126] = cases[index].track;
        tag[127] = cases[index].genre;
        TrackTags tags;
        CHECK(track_tags_read(memory_source(&fixture.memory), fixture.size, TAG_FORMAT_MP3, &tags));
        CHECK_EQUAL(0, strcmp(cases[index].expected_track, tags.text[TAG_TRACK]));
        CHECK_EQUAL(0, strcmp(cases[index].expected_genre, tags.text[TAG_GENRE]));
        free(fixture.bytes);
    }
}

static void id3_extended_headers_do_not_hide_following_frames(void)
{
    static const uint8_t plain[] = {3, 'N', 'e', 'w'};
    TrackTags tags;
    CHECK(read_id3_frame_with_header(3, 0, 0x40, "TIT2", plain, sizeof plain, &tags));
    CHECK_EQUAL(0, strcmp("New", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame_with_header(4, 0, 0x40, "TIT2", plain, sizeof plain, &tags));
    CHECK_EQUAL(0, strcmp("New", tags.text[TAG_TITLE]));
}

static void empty_id3_text_is_distinct_from_a_missing_frame(void)
{
    static const uint8_t empty[] = {0};
    static const uint8_t short_utf16[] = {1, 0xfe};
    TrackTags tags;
    CHECK(read_id3_frame(4, 0, "TIT2", empty, 0, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TITLE]));
    CHECK(read_id3_frame(4, 0, "TIT2", short_utf16, sizeof short_utf16, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TITLE]));
    static const uint8_t blank[] = {3, ' '};
    CHECK(read_id3_frame(4, 0, "TIT2", blank, sizeof blank, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TITLE]));
    static const uint8_t track[] = {3, '4', '2'};
    CHECK(read_id3_frame(4, 0, "TRCK", track, sizeof track, &tags));
    CHECK_EQUAL(0, strcmp("42", tags.text[TAG_TRACK]));
}

static void malformed_wma_content_cannot_read_an_artist_from_the_next_object(void)
{
    uint8_t bytes[88] = {
        0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0, 0xaa, 0, 0x62, 0xce,
        0x6c, 88,   0,    0,    0,    0,    0,    0,    0,    2,    0, 0,    0, 1,    2,
        0x33, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0, 0xaa, 0, 0x62, 0xce,
        0x6c, 34,   0,    0,    0,    0,    0,    0,    0,    2,    0, 2,    0, 0,    0,
        0,    0,    0,    0,    0,    0,    'Z',  0,    0,    0,    0, 0,    0, 0,    0,
        0,    0,    0,    0,    0,    24,   0,    0,    0,    0,    0, 0,    0,
    };
    MemorySource memory = {.bytes = bytes, .size = sizeof bytes};
    TrackTags tags;
    CHECK(track_tags_read(memory_source(&memory), sizeof bytes, TAG_FORMAT_WMA, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TITLE]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_ARTIST]));
    bytes[54] = 0;
    CHECK(track_tags_read(memory_source(&memory), sizeof bytes, TAG_FORMAT_WMA, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_ARTIST]));
}

typedef struct AsfFixture {
    uint8_t bytes[1024];
    uint32_t cursor;
    uint16_t attributes;
} AsfFixture;

static void asf_put_le(uint8_t *bytes, uint64_t number, uint32_t width)
{
    for (uint32_t index = 0; index < width; index++) {
        bytes[index] = (uint8_t)(number >> (index * 8));
    }
}

static void asf_start(AsfFixture *fixture)
{
    static const uint8_t header[] = {0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
                                     0xa6, 0xd9, 0,    0xaa, 0,    0x62, 0xce, 0x6c};
    static const uint8_t extended[] = {0x40, 0xa4, 0xd0, 0xd2, 0x07, 0xe3, 0xd2, 0x11,
                                       0x97, 0xf0, 0x00, 0xa0, 0xc9, 0x5e, 0xa8, 0x50};
    *fixture = (AsfFixture){.cursor = 56};
    memcpy(fixture->bytes, header, sizeof header);
    fixture->bytes[24] = 1;
    fixture->bytes[28] = 1;
    fixture->bytes[29] = 2;
    memcpy(fixture->bytes + 30, extended, sizeof extended);
}

static void asf_attribute(AsfFixture *fixture, const char *name, uint16_t type,
                          const uint8_t *value, uint16_t size)
{
    uint32_t length = (uint32_t)(strlen(name) + 1) * 2;
    asf_put_le(fixture->bytes + fixture->cursor, length, 2);
    fixture->cursor += 2;
    for (uint32_t index = 0; index < length / 2 - 1; index++) {
        fixture->bytes[fixture->cursor + index * 2] = (uint8_t)name[index];
    }
    fixture->cursor += length;
    asf_put_le(fixture->bytes + fixture->cursor, type, 2);
    asf_put_le(fixture->bytes + fixture->cursor + 2, size, 2);
    fixture->cursor += 4;
    memcpy(fixture->bytes + fixture->cursor, value, size);
    fixture->cursor += size;
    fixture->attributes++;
}

static bool asf_read(AsfFixture *fixture, TrackTags *tags)
{
    asf_put_le(fixture->bytes + 16, fixture->cursor, 8);
    asf_put_le(fixture->bytes + 46, fixture->cursor - 30, 8);
    asf_put_le(fixture->bytes + 54, fixture->attributes, 2);
    MemorySource source = {.bytes = fixture->bytes, .size = fixture->cursor};
    return track_tags_read(memory_source(&source), fixture->cursor, TAG_FORMAT_WMA, tags);
}

static void wma_extended_attributes_read_text_and_integer_widths(void)
{
    AsfFixture fixture;
    asf_start(&fixture);
    static const uint8_t title[] = {'A', 0, 'l', 0, 'b', 0, 0, 0};
    static const uint8_t number[] = {0x39, 0x30, 0, 0};
    static const uint8_t year[] = {0xe9, 0x07, 0, 0, 0, 0, 0, 0};
    static const uint8_t disc[] = {2, 0};
    asf_attribute(&fixture, "WM/AlbumTitle", 0, title, sizeof title);
    asf_attribute(&fixture, "WM/TrackNumber", 3, number, sizeof number);
    asf_attribute(&fixture, "WM/Year", 4, year, sizeof year);
    asf_attribute(&fixture, "WM/PartOfSet", 5, disc, sizeof disc);
    asf_attribute(&fixture, "Ignored", 3, number, sizeof number);
    TrackTags tags;
    CHECK(asf_read(&fixture, &tags));
    CHECK_EQUAL(0, strcmp("Alb", tags.text[TAG_ALBUM]));
    CHECK_EQUAL(0, strcmp("12345", tags.text[TAG_TRACK]));
    CHECK_EQUAL(0, strcmp("2025", tags.text[TAG_DATE]));
    CHECK_EQUAL(0, strcmp("2", tags.text[TAG_DISC]));
}

static void wma_picture_attributes_accept_front_and_reject_short_data(void)
{
    AsfFixture fixture;
    asf_start(&fixture);
    static const uint8_t picture[] = {3, 4, 0, 0, 0, 'i', 0, 0, 0, 0, 0, 0xff, 0xd8, 0xff, 0xd9};
    asf_attribute(&fixture, "WM/Picture", 1, picture, sizeof picture);
    TrackTags tags;
    CHECK(asf_read(&fixture, &tags));
    CHECK_EQUAL(TAG_PICTURE_RAW, tags.picture.kind);
    CHECK(tags.picture.front);
    CHECK_EQUAL(4, tags.picture.length);
    CHECK_EQUAL(0xff, fixture.bytes[tags.picture.offset]);
    asf_start(&fixture);
    asf_attribute(&fixture, "WM/Picture", 1, picture, 4);
    CHECK(asf_read(&fixture, &tags));
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
}

static void wma_extension_start(AsfFixture *fixture, bool library)
{
    static const uint8_t extension[] = {0xb5, 0x03, 0xbf, 0x5f, 0x2e, 0xa9, 0xcf, 0x11,
                                        0x8e, 0xe3, 0,    0xc0, 0x0c, 0x20, 0x53, 0x65};
    static const uint8_t metadata[] = {0xea, 0xcb, 0xf8, 0xc5, 0xaf, 0x5b, 0x77, 0x48,
                                       0x84, 0x67, 0xaa, 0x8c, 0x44, 0xfa, 0x4c, 0xca};
    static const uint8_t metadata_library[] = {0x94, 0x1c, 0x23, 0x44, 0x98, 0x94, 0xd1, 0x49,
                                               0xa1, 0x41, 0x1d, 0x13, 0x4e, 0x45, 0x70, 0x54};
    asf_start(fixture);
    memcpy(fixture->bytes + 30, extension, sizeof extension);
    memcpy(fixture->bytes + 76, library ? metadata_library : metadata, sizeof metadata);
    fixture->cursor = 102;
}

static void wma_metadata_record(AsfFixture *fixture, const char *name, uint16_t type,
                                const uint8_t *value, uint32_t size)
{
    uint32_t name_size = (uint32_t)(strlen(name) + 1) * 2;
    uint32_t start = fixture->cursor;
    asf_put_le(fixture->bytes + start + 4, name_size, 2);
    asf_put_le(fixture->bytes + start + 6, type, 2);
    asf_put_le(fixture->bytes + start + 8, size, 4);
    fixture->cursor += 12;
    for (uint32_t index = 0; index < name_size / 2 - 1; index++) {
        fixture->bytes[fixture->cursor + index * 2] = (uint8_t)name[index];
    }
    fixture->cursor += name_size;
    memcpy(fixture->bytes + fixture->cursor, value, size);
    fixture->cursor += size;
    fixture->attributes++;
}

static bool wma_read_extension(AsfFixture *fixture, TrackTags *tags)
{
    asf_put_le(fixture->bytes + 72, fixture->cursor - 76, 4);
    asf_put_le(fixture->bytes + 92, fixture->cursor - 76, 8);
    asf_put_le(fixture->bytes + 100, fixture->attributes, 2);
    return asf_read(fixture, tags);
}

static void wma_header_extensions_read_metadata_and_metadata_library(void)
{
    static const uint8_t artist[] = {'A', 0, 'r', 0, 't', 0, 0, 0};
    static const uint8_t number[] = {11, 0, 0, 0};
    for (uint32_t library = 0; library < 2; library++) {
        AsfFixture fixture;
        wma_extension_start(&fixture, library != 0);
        wma_metadata_record(&fixture, "WM/AlbumArtist", 0, artist, sizeof artist);
        wma_metadata_record(&fixture, "WM/TrackNumber", 3, number, sizeof number);
        TrackTags tags;
        CHECK(wma_read_extension(&fixture, &tags));
        CHECK_EQUAL(0, strcmp("Art", tags.text[TAG_ALBUM_ARTIST]));
        CHECK_EQUAL(0, strcmp("11", tags.text[TAG_TRACK]));
    }
}

static void wma_truncated_metadata_values_are_not_published(void)
{
    static const uint8_t number[] = {11, 0, 0, 0};
    AsfFixture fixture;
    wma_extension_start(&fixture, false);
    wma_metadata_record(&fixture, "WM/TrackNumber", 3, number, sizeof number);
    asf_put_le(fixture.bytes + 110, 255, 4);
    TrackTags tags;
    CHECK(wma_read_extension(&fixture, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TRACK]));
}

typedef struct FailingTagSource {
    ByteSource original;
    uint32_t calls;
    uint32_t fail_at;
} FailingTagSource;

static bool failing_tag_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FailingTagSource *source = context;
    source->calls++;
    if (source->calls == source->fail_at) {
        return false;
    }
    return source->original.read(source->original.context, offset, buffer, length);
}

static void metadata_read_failures_leave_terminated_fields_and_bounded_pictures(void)
{
    static const char *const names[] = {
        "id3v24.mp3", "tagged.wma", "tagged.flac", "tagged.ogg", "aac.m4a",
        "tagged.wav", "tagged.ape", "tagged.dsf",  "tagged.dff",
    };
    for (size_t format = 0; format < sizeof names / sizeof names[0]; format++) {
        Fixture fixture;
        CHECK(load(&fixture, names[format]));
        FailingTagSource source = {.original = memory_source(&fixture.memory)};
        ByteSource input = {.read = failing_tag_read, .context = &source};
        TrackTags tags;
        CHECK(
            track_tags_read(input, fixture.size, track_tags_format_for_name(names[format]), &tags));
        uint32_t total = source.calls;
        CHECK(total > 10);
        for (uint32_t failure = 1; failure <= total; failure++) {
            source.calls = 0;
            source.fail_at = failure;
            track_tags_read(input, fixture.size, track_tags_format_for_name(names[format]), &tags);
            for (int field = 0; field < TAG_FIELD_COUNT; field++) {
                CHECK(memchr(tags.text[field], '\0', TRACK_TAG_TEXT_CAPACITY) != NULL);
            }
            CHECK(tags.picture.kind == TAG_PICTURE_NONE ||
                  tags.picture.kind == TAG_PICTURE_BASE64_FLAC_BLOCK ||
                  (tags.picture.offset <= fixture.size &&
                   tags.picture.length <= fixture.size - tags.picture.offset));
        }
        free(fixture.bytes);
    }
}

static void wma_incomplete_numeric_attributes_and_picture_data_are_ignored(void)
{
    AsfFixture fixture;
    asf_start(&fixture);
    static const uint8_t short_value[] = {1};
    static const uint8_t bad_picture[] = {3, 20, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xd8};
    asf_attribute(&fixture, "WM/TrackNumber", 3, short_value, sizeof short_value);
    asf_attribute(&fixture, "WM/Year", 4, short_value, sizeof short_value);
    asf_attribute(&fixture, "WM/PartOfSet", 5, short_value, sizeof short_value);
    asf_attribute(&fixture, "WM/AlbumTitle", 2, short_value, sizeof short_value);
    asf_attribute(&fixture, "WM/Picture", 0, short_value, sizeof short_value);
    asf_attribute(&fixture, "WM/Picture", 1, bad_picture, sizeof bad_picture);
    char long_name[80];
    memset(long_name, 'A', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    asf_attribute(&fixture, long_name, 0, short_value, sizeof short_value);
    TrackTags tags;
    CHECK(asf_read(&fixture, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TRACK]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_DATE]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_DISC]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_ALBUM]));
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
}

static void wma_content_header_shorter_than_its_lengths_is_ignored(void)
{
    static const uint8_t content[] = {0x33, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
                                      0xa6, 0xd9, 0,    0xaa, 0,    0x62, 0xce, 0x6c};
    AsfFixture fixture;
    asf_start(&fixture);
    memcpy(fixture.bytes + 30, content, sizeof content);
    fixture.cursor = 58;
    TrackTags tags;
    CHECK(asf_read(&fixture, &tags));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TITLE]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_ARTIST]));
}

typedef struct FlacComments {
    uint8_t bytes[4096];
    uint32_t cursor;
    uint32_t count;
} FlacComments;

static void flac_comments_start(FlacComments *file)
{
    *file = (FlacComments){.cursor = 54};
    memcpy(file->bytes, "fLaC", 4);
    file->bytes[7] = 34;
    file->bytes[42] = 0x84;
}

static void flac_comment(FlacComments *file, const char *value)
{
    uint32_t length = (uint32_t)strlen(value);
    asf_put_le(file->bytes + file->cursor, length, 4);
    file->cursor += 4;
    memcpy(file->bytes + file->cursor, value, length);
    file->cursor += length;
    file->count++;
}

static bool flac_comments_read(FlacComments *file, TrackTags *tags)
{
    uint32_t length = file->cursor - 46;
    file->bytes[43] = (uint8_t)(length >> 16);
    file->bytes[44] = (uint8_t)(length >> 8);
    file->bytes[45] = (uint8_t)length;
    asf_put_le(file->bytes + 50, file->count, 4);
    MemorySource source = {.bytes = file->bytes, .size = file->cursor};
    return track_tags_read(memory_source(&source), file->cursor, TAG_FORMAT_FLAC, tags);
}

static void vorbis_comments_read_case_insensitive_fields_and_front_art(void)
{
    FlacComments file;
    flac_comments_start(&file);
    flac_comment(&file, "TITLE=First");
    flac_comment(&file, "title=Second");
    flac_comment(&file, "ArTiSt=Artist");
    flac_comment(&file, "ALBUMARTIST=Band");
    flac_comment(&file, "GENRE=Ambient");
    flac_comment(&file, "DISCNUMBER=2");
    flac_comment(&file, "TRACKNUMBER=4");
    flac_comment(&file, "NoEqualsSign");
    flac_comment(&file, "NotARecognisedName=Ignored");
    flac_comment(&file, "METADATA_BLOCK_PICTURE=AAAAAw==");
    TrackTags tags;
    CHECK(flac_comments_read(&file, &tags));
    CHECK_EQUAL(0, strcmp("First", tags.text[TAG_TITLE]));
    CHECK_EQUAL(0, strcmp("Artist", tags.text[TAG_ARTIST]));
    CHECK_EQUAL(0, strcmp("Band", tags.text[TAG_ALBUM_ARTIST]));
    CHECK_EQUAL(0, strcmp("Ambient", tags.text[TAG_GENRE]));
    CHECK_EQUAL(0, strcmp("2", tags.text[TAG_DISC]));
    CHECK_EQUAL(0, strcmp("4", tags.text[TAG_TRACK]));
    CHECK_EQUAL(TAG_PICTURE_BASE64_FLAC_BLOCK, tags.picture.kind);
    CHECK(tags.picture.front);
}

static void malformed_vorbis_entries_do_not_read_the_next_block(void)
{
    FlacComments file;
    flac_comments_start(&file);
    flac_comment(&file, "TITLE=Valid");
    flac_comment(&file, "ARTIST=No");
    asf_put_le(file.bytes + 69, 0xffff, 4);
    TrackTags tags;
    CHECK(flac_comments_read(&file, &tags));
    CHECK_EQUAL(0, strcmp("Valid", tags.text[TAG_TITLE]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_ARTIST]));
}

typedef struct Mp4TagFixture {
    uint8_t bytes[2048];
    uint32_t length;
} Mp4TagFixture;

static void mp4_put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t mp4_open_atom(Mp4TagFixture *file, const char *type)
{
    uint32_t start = file->length;
    memcpy(file->bytes + start + 4, type, 4);
    file->length += 8;
    return start;
}

static void mp4_close_atom(Mp4TagFixture *file, uint32_t start)
{
    mp4_put32(file->bytes + start, file->length - start);
}

static void mp4_item(Mp4TagFixture *file, const char *type, const uint8_t *value, uint32_t length)
{
    uint32_t item = mp4_open_atom(file, type);
    uint32_t data = mp4_open_atom(file, "data");
    file->length += 8;
    memcpy(file->bytes + file->length, value, length);
    file->length += length;
    mp4_close_atom(file, data);
    mp4_close_atom(file, item);
}

static bool mp4_tags(Mp4TagFixture *file, TrackTags *tags)
{
    MemorySource source = {.bytes = file->bytes, .size = file->length};
    return track_tags_read(memory_source(&source), file->length, TAG_FORMAT_MP4, tags);
}

static void mp4_tags_read_numeric_tracks_genre_fallback_and_art(void)
{
    Mp4TagFixture file = {0};
    uint32_t moov = mp4_open_atom(&file, "moov");
    uint32_t udta = mp4_open_atom(&file, "udta");
    uint32_t meta = mp4_open_atom(&file, "meta");
    file.length += 4;
    uint32_t ilst = mp4_open_atom(&file, "ilst");
    static const uint8_t title[] = {'S', 'o', 'n', 'g'};
    static const uint8_t track[] = {0, 0, 0, 12, 0, 0};
    static const uint8_t disc[] = {0, 0, 0, 2, 0, 0};
    static const uint8_t genre[] = {0, 18};
    static const uint8_t cover[] = {0xff, 0xd8, 0xff, 0xd9};
    mp4_item(&file, "\xa9nam", title, sizeof title);
    mp4_item(&file, "trkn", track, sizeof track);
    mp4_item(&file, "disk", disc, sizeof disc);
    mp4_item(&file, "gnre", genre, sizeof genre);
    mp4_item(&file, "covr", cover, sizeof cover);
    mp4_close_atom(&file, ilst);
    mp4_close_atom(&file, meta);
    mp4_close_atom(&file, udta);
    mp4_close_atom(&file, moov);
    TrackTags tags;
    CHECK(mp4_tags(&file, &tags));
    CHECK_EQUAL(0, strcmp("Song", tags.text[TAG_TITLE]));
    CHECK_EQUAL(0, strcmp("12", tags.text[TAG_TRACK]));
    CHECK_EQUAL(0, strcmp("2", tags.text[TAG_DISC]));
    CHECK_EQUAL(0, strcmp("Rock", tags.text[TAG_GENRE]));
    CHECK_EQUAL(TAG_PICTURE_RAW, tags.picture.kind);
    CHECK(tags.picture.front);
    CHECK_EQUAL(4, tags.picture.length);
    CHECK_EQUAL(0xff, file.bytes[tags.picture.offset]);
}

static Mp4TagFixture mp4_duration_file(uint8_t version, uint32_t length)
{
    Mp4TagFixture file = {0};
    uint32_t moov = mp4_open_atom(&file, "moov");
    uint32_t trak = mp4_open_atom(&file, "trak");
    uint32_t mdia = mp4_open_atom(&file, "mdia");
    uint32_t hdlr = mp4_open_atom(&file, "hdlr");
    memcpy(file.bytes + file.length + 8, "soun", 4);
    file.length += 12;
    mp4_close_atom(&file, hdlr);
    uint32_t mdhd = mp4_open_atom(&file, "mdhd");
    uint8_t *body = file.bytes + file.length;
    body[0] = version;
    if (version == 0 && length >= 20) {
        mp4_put32(body + 12, 1000);
        mp4_put32(body + 16, 2000);
    } else if (version == 1 && length >= 32) {
        mp4_put32(body + 20, 1000);
        mp4_put32(body + 28, 2000);
    }
    file.length += length;
    mp4_close_atom(&file, mdhd);
    mp4_close_atom(&file, mdia);
    mp4_close_atom(&file, trak);
    mp4_close_atom(&file, moov);
    return file;
}

static void mp4_duration_requires_a_complete_versioned_header(void)
{
    TrackTags tags;
    for (uint32_t length = 0; length < 20; length++) {
        Mp4TagFixture file = mp4_duration_file(0, length);
        CHECK(mp4_tags(&file, &tags));
        CHECK_EQUAL(0, tags.duration_ms);
    }
    for (uint32_t length = 1; length < 32; length++) {
        Mp4TagFixture file = mp4_duration_file(1, length);
        CHECK(mp4_tags(&file, &tags));
        CHECK_EQUAL(0, tags.duration_ms);
    }
    Mp4TagFixture version0 = mp4_duration_file(0, 20);
    CHECK(mp4_tags(&version0, &tags));
    CHECK_EQUAL(2000, tags.duration_ms);
    Mp4TagFixture version1 = mp4_duration_file(1, 32);
    CHECK(mp4_tags(&version1, &tags));
    CHECK_EQUAL(2000, tags.duration_ms);
}

static void mp4_text_genre_takes_priority_over_the_legacy_genre(void)
{
    Mp4TagFixture file = {0};
    uint32_t moov = mp4_open_atom(&file, "moov");
    uint32_t udta = mp4_open_atom(&file, "udta");
    uint32_t meta = mp4_open_atom(&file, "meta");
    file.length += 4;
    uint32_t ilst = mp4_open_atom(&file, "ilst");
    static const uint8_t legacy[] = {0, 18};
    static const uint8_t genre[] = "Electronic";
    static const uint8_t short_number[] = {0, 0, 0};
    mp4_item(&file, "gnre", legacy, sizeof legacy);
    mp4_item(&file, "\xa9gen", genre, sizeof genre - 1);
    mp4_item(&file, "trkn", short_number, sizeof short_number);
    mp4_item(&file, "covr", short_number, 0);
    mp4_close_atom(&file, ilst);
    mp4_close_atom(&file, meta);
    mp4_close_atom(&file, udta);
    mp4_close_atom(&file, moov);
    TrackTags tags;
    CHECK(mp4_tags(&file, &tags));
    CHECK_EQUAL(0, strcmp("Electronic", tags.text[TAG_GENRE]));
    CHECK_EQUAL(0, strcmp("", tags.text[TAG_TRACK]));
    CHECK_EQUAL(TAG_PICTURE_NONE, tags.picture.kind);
}

static void mp4_duration_ignores_unknown_versions_and_zero_timescales(void)
{
    TrackTags tags;
    Mp4TagFixture unknown = mp4_duration_file(2, 32);
    CHECK(mp4_tags(&unknown, &tags));
    CHECK_EQUAL(0, tags.duration_ms);
    Mp4TagFixture zero_scale = mp4_duration_file(0, 20);
    mp4_put32(zero_scale.bytes + zero_scale.length - 8, 0);
    CHECK(mp4_tags(&zero_scale, &tags));
    CHECK_EQUAL(0, tags.duration_ms);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(mp4_tags_read_numeric_tracks_genre_fallback_and_art),
        TEST_CASE(mp4_duration_requires_a_complete_versioned_header),
        TEST_CASE(mp4_text_genre_takes_priority_over_the_legacy_genre),
        TEST_CASE(mp4_duration_ignores_unknown_versions_and_zero_timescales),
        TEST_CASE(every_tagged_format_reads_its_fields_duration_and_cover),
        TEST_CASE(an_mp3_with_only_id3v1_and_no_xing_header_still_reads),
        TEST_CASE(an_untagged_flac_has_empty_fields_and_its_duration),
        TEST_CASE(formats_come_from_the_extension_in_any_case),
        TEST_CASE(a_truncated_file_fails_cleanly),
        TEST_CASE(damaged_files_never_read_outside_the_file),
        TEST_CASE(unicode_tags_trim_spaces_without_losing_characters),
        TEST_CASE(id3_genres_handle_numeric_special_and_escaped_values),
        TEST_CASE(id3_frame_versions_and_flags_preserve_usable_titles),
        TEST_CASE(id3_unsynchronised_text_restores_escaped_bytes),
        TEST_CASE(id3_picture_frames_accept_old_and_new_cover_layouts),
        TEST_CASE(id3v1_track_numbers_and_missing_genres_are_read_safely),
        TEST_CASE(id3_extended_headers_do_not_hide_following_frames),
        TEST_CASE(empty_id3_text_is_distinct_from_a_missing_frame),
        TEST_CASE(malformed_wma_content_cannot_read_an_artist_from_the_next_object),
        TEST_CASE(wma_extended_attributes_read_text_and_integer_widths),
        TEST_CASE(wma_picture_attributes_accept_front_and_reject_short_data),
        TEST_CASE(wma_header_extensions_read_metadata_and_metadata_library),
        TEST_CASE(wma_truncated_metadata_values_are_not_published),
        TEST_CASE(metadata_read_failures_leave_terminated_fields_and_bounded_pictures),
        TEST_CASE(wma_incomplete_numeric_attributes_and_picture_data_are_ignored),
        TEST_CASE(wma_content_header_shorter_than_its_lengths_is_ignored),
        TEST_CASE(vorbis_comments_read_case_insensitive_fields_and_front_art),
        TEST_CASE(malformed_vorbis_entries_do_not_read_the_next_block),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
