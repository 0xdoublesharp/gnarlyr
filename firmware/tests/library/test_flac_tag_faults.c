#include <string.h>

#include "check.h"
#include "library/library_scan.h"
#include "library/track_tags.h"

enum {
    AFTER_STREAMINFO = 42,
    CAPACITY = 16 * 1024
};

static const uint8_t FLAC[] = {
    'f',  'L',  'a',  'C', 0,   0,    0,    34,   0,    16,  0,   16,  0,  0, 0, 0, 0,   0,   0x0B,
    0xB8, 0x02, 0xF0, 0,   0,   0xBB, 0x80, 0,    0,    0,   0,   0,   0,  0, 0, 0, 0,   0,   0,
    0,    0,    0,    0,   4,   0,    0,    39,   0,    0,   0,   0,   2,  0, 0, 0, 11,  0,   0,
    0,    'T',  'I',  'T', 'L', 'E',  '=',  'T',  'i',  't', 'l', 'e', 12, 0, 0, 0, 'A', 'R', 'T',
    'I',  'S',  'T',  '=', 'B', 'a',  'n',  'd',  's',  6,   0,   0,   33, 0, 0, 0, 3,   0,   0,
    0,    0,    0,    0,   0,   0,    0,    0,    0,    1,   0,   0,   0,  1, 0, 0, 0,   24,  0,
    0,    0,    0,    0,   0,   0,    1,    0xFF, 0x81, 0,   0,   4,   0,  0, 0, 0};

typedef struct Media {
    uint32_t fail_at;
    uint32_t eligible;
    uint32_t opens;
    bool failed;
    bool listed;
    bool root;
    uint32_t published;
} Media;

static bool read_media(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    Media *media = context;
    CHECK(offset <= sizeof FLAC && length <= sizeof FLAC - offset);
    bool fail = offset >= AFTER_STREAMINFO && media->eligible++ == media->fail_at;
    memcpy(buffer, FLAC + offset, fail ? length / 2 : length);
    media->failed |= fail;
    return !fail;
}

static void check_tags(const TrackTags *tags, bool picture)
{
    CHECK(strcmp(tags->text[TAG_TITLE], "Title") == 0);
    CHECK(strcmp(tags->text[TAG_ARTIST], "Bands") == 0);
    CHECK_EQUAL(picture ? TAG_PICTURE_RAW : TAG_PICTURE_NONE, tags->picture.kind);
}

static void all_metadata_reads_fail_honestly_then_recover(void)
{
    Media media = {.fail_at = UINT32_MAX};
    ByteSource source = {read_media, &media};
    TrackTags tags;
    CHECK(track_tags_read(source, sizeof FLAC, TAG_FORMAT_FLAC, &tags));
    check_tags(&tags, true);
    uint32_t steps = media.eligible;
    CHECK(steps >= 12);
    for (uint32_t index = 0; index < steps; index++) {
        media = (Media){.fail_at = index};
        CHECK(!track_tags_read(source, sizeof FLAC, TAG_FORMAT_FLAC, &tags));
        CHECK(media.failed);
        CHECK(track_tags_read(source, sizeof FLAC, TAG_FORMAT_FLAC, &tags));
        check_tags(&tags, true);
    }
}

static bool open_dir(void *context, const char *path, const ScanPlace *place)
{
    (void)place;
    Media *media = context;
    media->listed = false;
    media->root = strcmp(path, "/") == 0;
    return media->root || strcmp(path, "/Music/") == 0;
}

static bool next_entry(void *context, ScanEntry *entry)
{
    Media *media = context;
    if (media->listed) {
        return false;
    }
    media->listed = true;
    *entry =
        (ScanEntry){.directory = media->root, .size = sizeof FLAC, .modified = 1, .cluster = 2};
    strcpy(entry->name, media->root ? "Music" : "song.flac");
    return true;
}

static bool open_media(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    Media *media = context;
    CHECK(strcmp(path, "/Music/song.flac") == 0);
    media->opens++;
    *source = (ByteSource){read_media, media};
    *size = sizeof FLAC;
    return true;
}

static void close_entry(void *context)
{
    (void)context;
}

static bool read_blob(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    memcpy(buffer, (uint8_t *)context + offset, length);
    return true;
}

static bool write_blob(void *context, uint32_t offset, const void *buffer, uint32_t length)
{
    memcpy((uint8_t *)context + offset, buffer, length);
    return true;
}

static bool observe_track(void *context, const BuilderTrack *track)
{
    Media *media = context;
    CHECK(strcmp(track->title, "Title") == 0);
    CHECK(strcmp(track->artist, "Bands") == 0);
    media->published++;
    return false;
}

static uint8_t cache[SCAN_TAG_CACHE_BYTES];

static ScanStats scan(Media *media)
{
    static uint8_t workspace[384 * 1024];
    static uint8_t output[CAPACITY];
    static uint8_t scratch[16 * 1024 * 1024];
    ScanJob job = {
        .files = {open_dir, next_entry, close_entry, open_media, close_entry, media},
        .memory = workspace,
        .size = sizeof workspace,
        .scratch = {read_blob, write_blob, scratch, sizeof scratch},
        .sink = {write_blob, output},
        .capacity = sizeof output,
        .filter = {.hidden = observe_track, .context = media},
        .tag_cache = {read_blob, write_blob, cache, sizeof cache},
    };
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, library_scan(&job, &stats));
    return stats;
}

static void failed_scans_reopen_then_cache_the_complete_tags(void)
{
    Media baseline = {.fail_at = UINT32_MAX};
    TrackTags tags;
    CHECK(track_tags_read_index((ByteSource){read_media, &baseline}, sizeof FLAC, TAG_FORMAT_FLAC,
                                &tags));
    check_tags(&tags, false);
    CHECK(baseline.eligible > 8);
    for (uint32_t index = 0; index < baseline.eligible; index++) {
        memset(cache, 0, sizeof cache);
        Media media = {.fail_at = index};
        ScanStats first = scan(&media);
        CHECK(media.failed);
        CHECK_EQUAL(1, first.skipped);
        CHECK_EQUAL(0, first.tracks);
        CHECK_EQUAL(0, media.published);
        CHECK_EQUAL(1, scan(&media).tracks);
        CHECK_EQUAL(2, media.opens);
        CHECK_EQUAL(1, scan(&media).tracks);
        CHECK_EQUAL(2, media.opens);
        CHECK_EQUAL(2, media.published);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(all_metadata_reads_fail_honestly_then_recover),
        TEST_CASE(failed_scans_reopen_then_cache_the_complete_tags),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
