#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "memory_source.h"
#include "mp4_chapters.h"

typedef struct ChapterFile {
    uint8_t *bytes;
    uint32_t length;
    MemorySource memory;
} ChapterFile;

typedef struct FailedRead {
    ByteSource source;
    uint32_t calls;
    uint32_t fail_at;
} FailedRead;

static bool read_failed(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FailedRead *failure = context;
    failure->calls++;
    return failure->calls != failure->fail_at &&
           failure->source.read(failure->source.context, offset, buffer, length);
}

static bool chapter_file(ChapterFile *file, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", ECHO_FIXTURE_DIR, name);
    FILE *input = fopen(path, "rb");
    if (input == NULL) {
        return false;
    }
    fseek(input, 0, SEEK_END);
    long size = ftell(input);
    fseek(input, 0, SEEK_SET);
    if (size <= 0) {
        fclose(input);
        return false;
    }
    file->length = (uint32_t)size;
    file->bytes = malloc(file->length);
    bool ok = file->bytes != NULL && fread(file->bytes, 1, file->length, input) == file->length;
    fclose(input);
    if (!ok) {
        free(file->bytes);
        return false;
    }
    file->memory = (MemorySource){.bytes = file->bytes, .size = file->length};
    return true;
}

static void real_chapter_files_keep_time_and_names(void)
{
    const char *names[] = {"m4b_real/Audiobooks/Real Book.m4b",
                           "m4b_real/Audiobooks/Real Track.m4b"};
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        ChapterFile file;
        if (!chapter_file(&file, names[index])) {
            CHECK(false);
            continue;
        }
        Mp4Chapters chapters;
        CHECK(mp4_chapters_open(&chapters, memory_source(&file.memory), file.length, 1000));
        CHECK_EQUAL(3, chapters.count);
        CHECK_EQUAL(0, chapters.start_ms[0]);
        CHECK_EQUAL(400, chapters.start_ms[1]);
        CHECK_EQUAL(700, chapters.start_ms[2]);
        char title[MP4_CHAPTER_TITLE];
        mp4_chapter_title(&chapters, 1, title);
        CHECK_EQUAL(0, strcmp("Middle", title));
        free(file.bytes);
    }
}

static void read_failures_do_not_publish_invalid_chapters(void)
{
    const char *names[] = {
        "m4b_real/Audiobooks/Real Book.m4b", "m4b_real/Audiobooks/Real Track.m4b",
        "m4b_music/Audiobooks/Track Book.m4b", "m4b_music/Audiobooks/Nero Book.m4b",
        "m4b_music/Audiobooks/Messy Chpl.m4b"};
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        ChapterFile file;
        if (!chapter_file(&file, names[index])) {
            CHECK(false);
            continue;
        }
        FailedRead failure = {.source = memory_source(&file.memory)};
        ByteSource input = {.read = read_failed, .context = &failure};
        Mp4Chapters chapters;
        CHECK(mp4_chapters_open(&chapters, input, file.length, 0));
        uint32_t reads = failure.calls;
        CHECK(reads > 3);
        for (uint32_t position = 1; position <= reads; position++) {
            failure.calls = 0;
            failure.fail_at = position;
            bool found = mp4_chapters_open(&chapters, input, file.length, 0);
            CHECK(found || chapters.count == 0);
            CHECK(chapters.count <= MP4_CHAPTER_MAX);
            for (uint32_t chapter = 0; chapter < chapters.count; chapter++) {
                CHECK(chapters.offset[chapter] <= file.length);
                CHECK(chapters.length[chapter] <= file.length - chapters.offset[chapter]);
                CHECK(chapter == 0 || chapters.start_ms[chapter] > chapters.start_ms[chapter - 1]);
                char title[MP4_CHAPTER_TITLE];
                mp4_chapter_title(&chapters, chapter, title);
                CHECK(memchr(title, '\0', sizeof title) != NULL);
            }
        }
        free(file.bytes);
    }
}

static void chapters_at_or_after_the_duration_are_excluded(void)
{
    const char *names[] = {"m4b_real/Audiobooks/Real Book.m4b",
                           "m4b_real/Audiobooks/Real Track.m4b"};
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        ChapterFile file;
        if (!chapter_file(&file, names[index])) {
            CHECK(false);
            continue;
        }
        Mp4Chapters chapters;
        CHECK(mp4_chapters_open(&chapters, memory_source(&file.memory), file.length, 600));
        CHECK_EQUAL(2, chapters.count);
        CHECK_EQUAL(400, chapters.start_ms[1]);
        CHECK(!mp4_chapters_open(&chapters, memory_source(&file.memory), file.length, 400));
        CHECK_EQUAL(0, chapters.count);
        free(file.bytes);
    }
}

static void truncated_files_never_publish_out_of_bounds_chapters(void)
{
    const char *names[] = {"m4b_real/Audiobooks/Real Book.m4b",
                           "m4b_real/Audiobooks/Real Track.m4b"};
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        ChapterFile file;
        if (!chapter_file(&file, names[index])) {
            CHECK(false);
            continue;
        }
        for (uint32_t cut = 0; cut < file.length; cut += 1 + file.length / 400) {
            file.memory.size = cut;
            Mp4Chapters chapters;
            bool found = mp4_chapters_open(&chapters, memory_source(&file.memory), cut, 1000);
            CHECK(found || chapters.count == 0);
            CHECK(chapters.count <= MP4_CHAPTER_MAX);
            for (uint32_t chapter = 0; chapter < chapters.count; chapter++) {
                CHECK(chapters.offset[chapter] <= cut);
                CHECK(chapters.length[chapter] <= cut - chapters.offset[chapter]);
                char title[MP4_CHAPTER_TITLE];
                mp4_chapter_title(&chapters, chapter, title);
                CHECK(memchr(title, '\0', sizeof title) != NULL);
            }
        }
        free(file.bytes);
    }
}

typedef struct ChapterBuffer {
    uint8_t bytes[4096];
    uint32_t length;
} ChapterBuffer;

static void chapter_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t chapter_atom(ChapterBuffer *file, const char *name)
{
    uint32_t start = file->length;
    memcpy(file->bytes + start + 4, name, 4);
    file->length += 8;
    return start;
}

static void chapter_close(ChapterBuffer *file, uint32_t start)
{
    chapter_be32(file->bytes + start, file->length - start);
}

static void chapter_entry(ChapterBuffer *file, uint64_t ticks, const char *name)
{
    for (uint32_t index = 0; index < 8; index++) {
        file->bytes[file->length + 7 - index] = (uint8_t)(ticks >> (index * 8));
    }
    file->length += 8;
    size_t size = strlen(name);
    file->bytes[file->length++] = (uint8_t)size;
    memcpy(file->bytes + file->length, name, size);
    file->length += (uint32_t)size;
}

static ChapterBuffer unusual_chapter_file(void)
{
    ChapterBuffer file = {0};
    uint32_t moov = chapter_atom(&file, "moov");
    uint32_t udta = chapter_atom(&file, "udta");
    uint32_t chpl = chapter_atom(&file, "chpl");
    file.length += 8;
    file.bytes[file.length++] = 5;
    chapter_entry(&file, 2000000, "Intro");
    chapter_entry(&file, 1000000, "Earlier");
    chapter_entry(&file, 4000000, "Middle");
    chapter_entry(&file, 4000000, "Duplicate");
    chapter_entry(&file, 9000000, "Too late");
    chapter_close(&file, chpl);
    chapter_close(&file, udta);
    chapter_close(&file, moov);
    return file;
}

static void chapter_lists_discard_duplicates_and_times_beyond_the_end(void)
{
    ChapterBuffer file = unusual_chapter_file();
    MemorySource memory = {.bytes = file.bytes, .size = file.length};
    Mp4Chapters chapters;
    CHECK(mp4_chapters_open(&chapters, memory_source(&memory), file.length, 800));
    CHECK_EQUAL(3, chapters.count);
    CHECK_EQUAL(0, chapters.start_ms[0]);
    CHECK_EQUAL(100, chapters.start_ms[1]);
    CHECK_EQUAL(400, chapters.start_ms[2]);
    char title[MP4_CHAPTER_TITLE];
    mp4_chapter_title(&chapters, 2, title);
    CHECK_EQUAL(0, strcmp("Middle", title));
}

static void a_chapter_entry_claiming_bytes_outside_its_atom_is_rejected(void)
{
    ChapterBuffer file = unusual_chapter_file();
    file.bytes[41] = 255;
    MemorySource memory = {.bytes = file.bytes, .size = file.length};
    Mp4Chapters chapters;
    CHECK(!mp4_chapters_open(&chapters, memory_source(&memory), file.length, 800));
    CHECK_EQUAL(0, chapters.count);
}

static void short_chapter_headers_and_unicode_titles_are_handled_safely(void)
{
    ChapterBuffer file = {0};
    uint32_t moov = chapter_atom(&file, "moov");
    uint32_t udta = chapter_atom(&file, "udta");
    uint32_t chpl = chapter_atom(&file, "chpl");
    file.length += 4;
    file.bytes[file.length++] = 2;
    char long_title[80];
    memset(long_title, 'a', 62);
    memcpy(long_title + 62, "\xe2\x82\xac", 4);
    chapter_entry(&file, 0, long_title);
    chapter_entry(&file, 1000000,
                  "\xfe\xff"
                  "A");
    chapter_close(&file, chpl);
    chapter_close(&file, udta);
    chapter_close(&file, moov);
    MemorySource memory = {.bytes = file.bytes, .size = file.length};
    Mp4Chapters chapters;
    CHECK(mp4_chapters_open(&chapters, memory_source(&memory), file.length, 0));
    CHECK_EQUAL(2, chapters.count);
    char title[MP4_CHAPTER_TITLE];
    mp4_chapter_title(&chapters, 0, title);
    CHECK_EQUAL(62u, strlen(title));
    CHECK(memcmp(title, long_title, 62) == 0);
    mp4_chapter_title(&chapters, 1, title);
    CHECK_EQUAL(0, strcmp("", title));
}

static void corrupt_atom_fields_never_publish_out_of_bounds_chapters(void)
{
    const char *names[] = {"m4b_real/Audiobooks/Real Book.m4b",
                           "m4b_real/Audiobooks/Real Track.m4b"};
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        ChapterFile file;
        if (!chapter_file(&file, names[index])) {
            CHECK(false);
            continue;
        }
        uint8_t *bytes = malloc(file.length);
        if (bytes == NULL) {
            CHECK(false);
            free(file.bytes);
            continue;
        }
        for (uint32_t offset = 0; offset + 4 <= file.length; offset += 1 + file.length / 800) {
            memcpy(bytes, file.bytes, file.length);
            memset(bytes + offset, 0xff, 4);
            MemorySource memory = {.bytes = bytes, .size = file.length};
            Mp4Chapters chapters;
            bool found = mp4_chapters_open(&chapters, memory_source(&memory), file.length, 1000);
            CHECK(found || chapters.count == 0);
            CHECK(chapters.count <= MP4_CHAPTER_MAX);
            for (uint32_t chapter = 0; chapter < chapters.count; chapter++) {
                CHECK(chapters.offset[chapter] <= file.length);
                CHECK(chapters.length[chapter] <= file.length - chapters.offset[chapter]);
                CHECK(chapter == 0 || chapters.start_ms[chapter] > chapters.start_ms[chapter - 1]);
                char title[MP4_CHAPTER_TITLE];
                mp4_chapter_title(&chapters, chapter, title);
                CHECK(memchr(title, '\0', sizeof title) != NULL);
            }
        }
        free(bytes);
        free(file.bytes);
    }
}

static bool chapter_table_name(const uint8_t *bytes)
{
    static const char *const names[] = {"stts", "stsz", "stsc", "stco", "co64",
                                        "tkhd", "mdhd", "chap", "chpl"};
    for (size_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        if (memcmp(bytes, names[index], 4) == 0) {
            return true;
        }
    }
    return false;
}

static void damaged_chapter_tables_cannot_publish_invalid_locations(void)
{
    ChapterFile file;
    if (!chapter_file(&file, "m4b_real/Audiobooks/Real Track.m4b")) {
        CHECK(false);
        return;
    }
    uint8_t *bytes = malloc(file.length);
    if (bytes == NULL) {
        CHECK(false);
        free(file.bytes);
        return;
    }
    uint32_t tables = 0;
    for (uint32_t atom = 4; atom + 36 <= file.length; atom++) {
        if (!chapter_table_name(file.bytes + atom)) {
            continue;
        }
        tables++;
        for (uint32_t field = atom - 4; field < atom + 32; field += 4) {
            for (int fill = 0; fill < 2; fill++) {
                memcpy(bytes, file.bytes, file.length);
                memset(bytes + field, fill == 0 ? 0 : 0xff, 4);
                MemorySource memory = {.bytes = bytes, .size = file.length};
                Mp4Chapters chapters;
                bool found =
                    mp4_chapters_open(&chapters, memory_source(&memory), file.length, 1000);
                CHECK(found || chapters.count == 0);
                CHECK(chapters.count <= MP4_CHAPTER_MAX);
                for (uint32_t index = 0; index < chapters.count; index++) {
                    CHECK(chapters.offset[index] <= file.length);
                    CHECK(chapters.length[index] <= file.length - chapters.offset[index]);
                    CHECK(index == 0 || chapters.start_ms[index] > chapters.start_ms[index - 1]);
                }
            }
        }
    }
    CHECK(tables >= 5);
    free(bytes);
    free(file.bytes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(real_chapter_files_keep_time_and_names),
        TEST_CASE(read_failures_do_not_publish_invalid_chapters),
        TEST_CASE(chapters_at_or_after_the_duration_are_excluded),
        TEST_CASE(truncated_files_never_publish_out_of_bounds_chapters),
        TEST_CASE(chapter_lists_discard_duplicates_and_times_beyond_the_end),
        TEST_CASE(a_chapter_entry_claiming_bytes_outside_its_atom_is_rejected),
        TEST_CASE(short_chapter_headers_and_unicode_titles_are_handled_safely),
        TEST_CASE(corrupt_atom_fields_never_publish_out_of_bounds_chapters),
        TEST_CASE(damaged_chapter_tables_cannot_publish_invalid_locations),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
