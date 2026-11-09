#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "library/cover_build.h"
#include "library/covers.h"
#include "library/library.h"
#include "library/library_scan.h"

enum {
    MAX_FILES = 64,
    PATH_CAPACITY = 64,
    COVER_SIDE = 112,
    PNG_ROW = 1 + COVER_SIDE * 3,
    PNG_RAW = COVER_SIDE * PNG_ROW,
    PNG_CAPACITY = PNG_RAW + 128,
    MEMORY_SIZE = 384 * 1024,
    LIBRARY_CAPACITY = 4 * 1024 * 1024,
    COVERS_CAPACITY = 1024 * 1024,
    SCRATCH_SIZE = 16 * 1024 * 1024,
    RECORD_SLOT = 8 + 4 + COVER_BUILD_RECORD_BYTES,
    COVER_FULL_BYTES = 112 * 112 * 2
};

typedef struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint16_t pixel;
} Color;

static const Color RED = {255, 0, 0, 0xF800};
static const Color GREEN = {0, 255, 0, 0x07E0};
static const Color BLUE = {0, 0, 255, 0x001F};
static const Color WHITE = {255, 255, 255, 0xFFFF};

typedef struct CardFile {
    char path[PATH_CAPACITY];
    const uint8_t *bytes;
    uint32_t size;
    uint32_t modified;
} CardFile;

typedef struct Card {
    CardFile files[MAX_FILES];
    uint32_t count;
    ScanEntry listing[MAX_FILES];
    uint32_t listed;
    uint32_t cursor;
    uint8_t pngs[MAX_FILES][PNG_CAPACITY];
    uint32_t png_count;
    uint8_t *flac;
    uint32_t flac_size;
} Card;

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t capacity;
} Buffer;

static Card card;
static Buffer library_file;
static Buffer covers_file;
static Buffer scratch_file;
static uint32_t covers_capacity = COVERS_CAPACITY;
static uint32_t dir_opens;
static uint32_t record_writes;
static uint32_t failing_record;
static void (*on_progress)(uint32_t done);

static bool child_of(const char *dir, const char *path, char *name, bool *directory)
{
    size_t length = strlen(dir);
    while (length > 0 && dir[length - 1] == '/') {
        length--;
    }
    size_t prefix = length + 1;
    if (strncmp(path, dir, length) != 0 || path[length] != '/') {
        return false;
    }
    const char *rest = path + prefix;
    const char *slash = strchr(rest, '/');
    size_t name_length = slash != NULL ? (size_t)(slash - rest) : strlen(rest);
    memcpy(name, rest, name_length);
    name[name_length] = '\0';
    *directory = slash != NULL;
    return name_length > 0;
}

static void list_child(const char *name, bool directory, uint32_t modified)
{
    for (uint32_t index = 0; index < card.listed; index++) {
        if (strcmp(card.listing[index].name, name) == 0) {
            return;
        }
    }
    ScanEntry *entry = &card.listing[card.listed++];
    entry->directory = directory;
    entry->modified = directory ? 0 : modified;
    snprintf(entry->name, sizeof entry->name, "%s", name);
}

static bool card_open_dir(void *context, const char *path)
{
    (void)context;
    dir_opens++;
    card.listed = 0;
    card.cursor = 0;
    for (uint32_t index = 0; index < card.count; index++) {
        char name[PATH_CAPACITY];
        bool directory = false;
        if (child_of(path, card.files[index].path, name, &directory)) {
            list_child(name, directory, card.files[index].modified);
        }
    }
    return card.listed > 0;
}

static bool card_next_entry(void *context, ScanEntry *entry)
{
    (void)context;
    if (card.cursor == card.listed) {
        return false;
    }
    *entry = card.listing[card.cursor++];
    return true;
}

static void card_close_dir(void *context)
{
    (void)context;
}

static bool card_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const CardFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(buffer, file->bytes + offset, length);
    return true;
}

static bool card_open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    (void)context;
    for (uint32_t index = 0; index < card.count; index++) {
        if (strcmp(card.files[index].path, path) == 0) {
            *source = (ByteSource){.read = card_read, .context = &card.files[index]};
            *size = card.files[index].size;
            return true;
        }
    }
    return false;
}

static void card_close_file(void *context)
{
    (void)context;
}

static bool buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const Buffer *buffer = context;
    if (offset > buffer->capacity || length > buffer->capacity - offset) {
        return false;
    }
    memcpy(bytes, buffer->bytes + offset, length);
    return true;
}

static bool buffer_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->capacity || length > buffer->capacity - offset) {
        return false;
    }
    memcpy(buffer->bytes + offset, bytes, length);
    return true;
}

static bool covers_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    if (length == COVER_FULL_BYTES && ++record_writes == failing_record) {
        return false;
    }
    return buffer_write(context, offset, bytes, length);
}

static void progress_report(void *context, uint32_t done, uint32_t total)
{
    (void)context;
    (void)total;
    if (on_progress != NULL) {
        on_progress(done);
    }
}

static uint8_t *put_be32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
    return at + 4;
}

static uint8_t *put_chunk_start(uint8_t *at, uint32_t length, const char *type)
{
    at = put_be32(at, length);
    memcpy(at, type, 4);
    return at + 4;
}

static uint32_t adler32(const uint8_t *bytes, uint32_t length)
{
    uint32_t low = 1;
    uint32_t high = 0;
    for (uint32_t index = 0; index < length; index++) {
        low = (low + bytes[index]) % 65521u;
        high = (high + low) % 65521u;
    }
    return high << 16 | low;
}

static void fill_rows(uint8_t *raw, Color color)
{
    for (uint32_t row = 0; row < COVER_SIDE; row++) {
        uint8_t *line = raw + row * PNG_ROW;
        line[0] = 0;
        for (uint32_t column = 0; column < COVER_SIDE; column++) {
            line[1 + column * 3] = color.red;
            line[2 + column * 3] = color.green;
            line[3 + column * 3] = color.blue;
        }
    }
}

static uint32_t make_png(uint8_t *png, Color color)
{
    static uint8_t raw[PNG_RAW];
    static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    static const uint8_t HEADER_TAIL[5] = {8, 2, 0, 0, 0};
    static const uint8_t STORED[7] = {0x78,
                                      0x01,
                                      0x01,
                                      PNG_RAW & 0xFF,
                                      PNG_RAW >> 8,
                                      (uint8_t)~PNG_RAW,
                                      (uint8_t)(~PNG_RAW >> 8)};
    fill_rows(raw, color);
    uint8_t *at = png;
    memcpy(at, SIGNATURE, sizeof SIGNATURE);
    at = put_chunk_start(at + sizeof SIGNATURE, 13, "IHDR");
    at = put_be32(put_be32(at, COVER_SIDE), COVER_SIDE);
    memcpy(at, HEADER_TAIL, sizeof HEADER_TAIL);
    at = put_be32(at + sizeof HEADER_TAIL, 0);
    at = put_chunk_start(at, sizeof STORED + PNG_RAW + 4, "IDAT");
    memcpy(at, STORED, sizeof STORED);
    memcpy(at + sizeof STORED, raw, PNG_RAW);
    at = put_be32(put_be32(at + sizeof STORED + PNG_RAW, adler32(raw, PNG_RAW)), 0);
    at = put_be32(put_chunk_start(at, 0, "IEND"), 0);
    return (uint32_t)(at - png);
}

static uint8_t *read_fixture(const char *name, uint32_t *size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    fseek(file, 0, SEEK_END);
    *size = (uint32_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = malloc(*size);
    CHECK(fread(bytes, 1, *size, file) == *size);
    fclose(file);
    return bytes;
}

static void add_file(const char *path, const uint8_t *bytes, uint32_t size)
{
    CardFile *file = &card.files[card.count++];
    snprintf(file->path, sizeof file->path, "%s", path);
    file->bytes = bytes;
    file->size = size;
    file->modified = 1000;
}

static void add_album(const char *album, Color color)
{
    char path[PATH_CAPACITY];
    uint8_t *png = card.pngs[card.png_count++];
    snprintf(path, sizeof path, "/%s/01.flac", album);
    add_file(path, card.flac, card.flac_size);
    snprintf(path, sizeof path, "/%s/cover.png", album);
    add_file(path, png, make_png(png, color));
}

static CardFile *find_file(const char *path)
{
    for (uint32_t index = 0; index < card.count; index++) {
        if (strcmp(card.files[index].path, path) == 0) {
            return &card.files[index];
        }
    }
    return NULL;
}

static void replace_cover(const char *album, Color color)
{
    char path[PATH_CAPACITY];
    snprintf(path, sizeof path, "/%s/cover.png", album);
    CardFile *file = find_file(path);
    CHECK(file != NULL);
    uint8_t *png = card.pngs[card.png_count++];
    file->size = make_png(png, color);
    file->bytes = png;
    file->modified++;
}

static void remove_album(const char *album)
{
    char prefix[PATH_CAPACITY];
    int length = snprintf(prefix, sizeof prefix, "/%s/", album);
    uint32_t kept = 0;
    for (uint32_t index = 0; index < card.count; index++) {
        if (strncmp(card.files[index].path, prefix, (size_t)length) != 0) {
            card.files[kept++] = card.files[index];
        }
    }
    card.count = kept;
}

static void reset(void)
{
    if (card.flac == NULL) {
        card.flac = read_fixture("tags/untagged.flac", &card.flac_size);
        library_file = (Buffer){calloc(LIBRARY_CAPACITY, 1), LIBRARY_CAPACITY};
        covers_file = (Buffer){calloc(COVERS_CAPACITY, 1), COVERS_CAPACITY};
        scratch_file = (Buffer){calloc(SCRATCH_SIZE, 1), SCRATCH_SIZE};
    }
    card.count = 0;
    card.png_count = 0;
    covers_capacity = COVERS_CAPACITY;
    record_writes = 0;
    failing_record = 0;
    on_progress = NULL;
    memset(covers_file.bytes, 0, COVERS_CAPACITY);
}

static ScanResult rescan(bool keep, CoverStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    ScanFileSystem files = {card_open_dir,  card_next_entry, card_close_dir,
                            card_open_file, card_close_file, NULL};
    ScanStats scan_stats;
    memset(library_file.bytes, 0, LIBRARY_CAPACITY);
    ScanJob scan = {.files = files,
                    .memory = memory,
                    .size = sizeof memory,
                    .scratch = {buffer_read, buffer_write, &scratch_file, SCRATCH_SIZE},
                    .sink = {buffer_write, &library_file},
                    .capacity = LIBRARY_CAPACITY};
    CHECK_EQUAL(SCAN_OK, library_scan(&scan, &scan_stats));
    covers_file.capacity = covers_capacity;
    dir_opens = 0;
    CoverJob job = {.files = files,
                    .library = {buffer_read, &library_file},
                    .library_size = LIBRARY_CAPACITY,
                    .sink = {covers_write, &covers_file},
                    .previous = {keep ? buffer_read : NULL, &covers_file},
                    .capacity = covers_capacity,
                    .memory = memory,
                    .size = sizeof memory,
                    .progress = {progress_report, NULL}};
    return cover_build(&job, stats);
}

static uint32_t album_key(const char *title)
{
    Library library;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&library, (ByteSource){buffer_read, &library_file}, LIBRARY_CAPACITY));
    for (uint32_t index = 0; index < library_count(&library, LIBRARY_ALBUMS); index++) {
        LibraryAlbum album;
        char text[PATH_CAPACITY];
        CHECK(library_read_album(&library, index, &album));
        CHECK(library_read_string(&library, album.title, text, sizeof text));
        if (strcmp(text, title) == 0) {
            return library_album_key(&library, index);
        }
    }
    CHECK(false);
    return 0;
}

static uint16_t cover_pixel(const char *title)
{
    static uint16_t pixels[COVER_FULL_PIXELS];
    Covers covers;
    uint32_t record = 0;
    CHECK(covers_open(&covers, (ByteSource){buffer_read, &covers_file}, covers_capacity));
    if (!covers_find(&covers, album_key(title), &record)) {
        return 0;
    }
    CHECK(covers_read(&covers, record, COVER_FULL, pixels));
    return pixels[COVER_FULL_PIXELS / 2];
}

static void three_albums(void)
{
    reset();
    add_album("Alpha", RED);
    add_album("Beta", GREEN);
    add_album("Gamma", BLUE);
}

static void the_first_build_decodes_every_album(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.albums);
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void an_unchanged_card_reuses_every_cover(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(3u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void a_new_album_is_the_only_one_decoded(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    add_album("Delta", WHITE);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(4u, stats.covers);
    CHECK_EQUAL(3u, stats.reused);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Delta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void replaced_art_is_decoded_again(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    replace_cover("Beta", WHITE);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Beta"));
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
}

static void a_removed_album_frees_its_place_for_a_new_one(void)
{
    CoverStats stats;
    three_albums();
    covers_capacity = COVERS_HEADER_SIZE + 3 * RECORD_SLOT;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    remove_album("Beta");
    add_album("Delta", WHITE);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Delta"));
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void an_album_without_art_loses_its_cover(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    remove_album("Beta");
    add_file("/Beta/01.flac", card.flac, card.flac_size);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(2u, stats.covers);
    CHECK_EQUAL(1u, stats.missing);
    CHECK_EQUAL(0u, cover_pixel("Beta"));
}

static void a_file_without_stamps_is_built_again(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    memset(covers_file.bytes + 12, 0, 4);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
}

static void a_damaged_index_is_ignored(void)
{
    CoverStats stats;
    uint8_t first[8];
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    memcpy(first, covers_file.bytes + COVERS_HEADER_SIZE, sizeof first);
    memcpy(covers_file.bytes + COVERS_HEADER_SIZE, covers_file.bytes + COVERS_HEADER_SIZE + 8, 8);
    memcpy(covers_file.bytes + COVERS_HEADER_SIZE + 8, first, sizeof first);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
}

static void without_a_previous_file_everything_is_decoded(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(SCAN_OK, rescan(false, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
}

static void add_blob_album(const char *album, const uint8_t *bytes, uint32_t size)
{
    char path[PATH_CAPACITY];
    uint8_t *copy = card.pngs[card.png_count++];
    CHECK(size <= PNG_CAPACITY);
    memcpy(copy, bytes, size);
    snprintf(path, sizeof path, "/%s/01.flac", album);
    add_file(path, card.flac, card.flac_size);
    snprintf(path, sizeof path, "/%s/cover.png", album);
    add_file(path, copy, size);
}

static void add_damaged_png(const char *album, uint32_t variant)
{
    static uint8_t png[PNG_CAPACITY];
    uint32_t size = make_png(png, RED);
    switch (variant) {
    case 0:
        size = size / 2;
        break;
    case 1:
        memset(png + 41, 0xFF, 16);
        break;
    case 2:
        put_be32(png + 16, 100000);
        put_be32(png + 20, 100000);
        break;
    case 3:
        png[24] = 16;
        png[28] = 1;
        size = 60;
        break;
    case 4:
        size = 8;
        break;
    default:
        png[25] = 7;
        break;
    }
    add_blob_album(album, png, size);
}

static void a_broken_cover_costs_only_its_own_album(void)
{
    static const uint8_t ARITHMETIC[] = {0xFF, 0xD8, 0xFF, 0xC9, 0,    11, 8,    0,   16,
                                         0,    16,   1,    1,    0x11, 0,  0xFF, 0xD9};
    static const uint8_t TWELVE_BIT[] = {0xFF, 0xD8, 0xFF, 0xC0, 0,    11, 12,   0,   16,
                                         0,    16,   1,    1,    0x11, 0,  0xFF, 0xD9};
    static const uint8_t CMYK[] = {0xFF, 0xD8, 0xFF, 0xC2, 0,    20, 8,    0,    16,
                                   0,    16,   4,    1,    0x11, 0,  2,    0x11, 0,
                                   3,    0x11, 0,    4,    0x11, 0,  0xFF, 0xD9};
    static const uint8_t HUGE[] = {0xFF, 0xD8, 0xFF, 0xC0, 0,    11, 8,    0xFF, 0xFF,
                                   0xFF, 0xFF, 1,    1,    0x11, 0,  0xFF, 0xD9};
    static const uint8_t NO_SCAN[] = {0xFF, 0xD8, 0xFF, 0xC0, 0,    11, 8,    0x40, 0,
                                      0x40, 0,    1,    1,    0x11, 0,  0xFF, 0xD9};
    static const uint8_t GARBAGE[] = {'G', 'I', 'F', '8', '9', 'a', 1, 2, 3, 4};
    CoverStats stats;
    char name[16];
    uint32_t broken = 0;
    reset();
    add_album("Alpha", RED);
    for (uint32_t variant = 0; variant < 6; variant++) {
        snprintf(name, sizeof name, "Bad%u", variant);
        add_damaged_png(name, variant);
        broken++;
    }
    add_blob_album("Bad6", ARITHMETIC, sizeof ARITHMETIC);
    add_blob_album("Bad7", TWELVE_BIT, sizeof TWELVE_BIT);
    add_blob_album("Bad8", CMYK, sizeof CMYK);
    add_blob_album("Bad9", HUGE, sizeof HUGE);
    add_blob_album("BadA", NO_SCAN, sizeof NO_SCAN);
    add_blob_album("BadB", GARBAGE, sizeof GARBAGE);
    add_blob_album("BadC", GARBAGE, 0);
    add_album("Zeta", BLUE);
    broken += 7;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(2u, stats.covers);
    CHECK_EQUAL(broken, stats.missing);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Zeta"));
    CHECK_EQUAL(0u, cover_pixel("Bad0"));
}

static void a_failed_record_write_costs_only_its_own_album(void)
{
    CoverStats stats;
    three_albums();
    failing_record = 2;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(2u, stats.covers);
    CHECK_EQUAL(1u, stats.missing);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(0u, cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
    failing_record = 0;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
}

static void a_failed_rewrite_drops_only_the_damaged_record(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    replace_cover("Beta", WHITE);
    record_writes = 0;
    failing_record = 1;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(2u, stats.covers);
    CHECK_EQUAL(0u, cover_pixel("Beta"));
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static uint32_t previous_covers_seen;

static void count_previous_covers(uint32_t done)
{
    Covers covers;
    if (done == 1 || done == 3) {
        CHECK(covers_open(&covers, (ByteSource){buffer_read, &covers_file}, covers_capacity));
        previous_covers_seen += covers.count;
    }
}

static void the_previous_covers_stay_readable_until_the_pass_ends(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    replace_cover("Gamma", WHITE);
    previous_covers_seen = 0;
    on_progress = count_previous_covers;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(6u, previous_covers_seen);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Gamma"));
}

static void an_album_spread_over_many_folders_lists_a_bounded_number_of_them(void)
{
    static uint8_t *tagged;
    static uint32_t tagged_size;
    CoverStats stats;
    char path[PATH_CAPACITY];
    reset();
    if (tagged == NULL) {
        tagged = read_fixture("tags/tagged.ogg", &tagged_size);
    }
    for (uint32_t folder = 0; folder < 40; folder++) {
        snprintf(path, sizeof path, "/Mix/d%02u/01.ogg", folder);
        add_file(path, tagged, tagged_size);
    }
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(1u, stats.albums);
    CHECK_EQUAL(1u, stats.missing);
    CHECK(dir_opens <= 16);
}

static void albums_sharing_a_folder_list_it_once(void)
{
    static const char *const NAMES[] = {"tags/id3v1_no_xing.mp3", "tags/tagged.ogg",
                                        "tags/untagged.flac"};
    static uint8_t *files[3];
    static uint32_t sizes[3];
    CoverStats stats;
    char path[PATH_CAPACITY];
    reset();
    for (uint32_t index = 0; index < 3; index++) {
        if (files[index] == NULL) {
            files[index] = read_fixture(NAMES[index], &sizes[index]);
        }
        snprintf(path, sizeof path, "/Flat/%s", strrchr(NAMES[index], '/') + 1);
        add_file(path, files[index], sizes[index]);
    }
    uint8_t *png = card.pngs[card.png_count++];
    add_file("/Flat/cover.png", png, make_png(png, GREEN));
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.albums);
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(1u, dir_opens);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_first_build_decodes_every_album),
        TEST_CASE(an_unchanged_card_reuses_every_cover),
        TEST_CASE(a_new_album_is_the_only_one_decoded),
        TEST_CASE(replaced_art_is_decoded_again),
        TEST_CASE(a_removed_album_frees_its_place_for_a_new_one),
        TEST_CASE(an_album_without_art_loses_its_cover),
        TEST_CASE(a_file_without_stamps_is_built_again),
        TEST_CASE(a_damaged_index_is_ignored),
        TEST_CASE(without_a_previous_file_everything_is_decoded),
        TEST_CASE(a_broken_cover_costs_only_its_own_album),
        TEST_CASE(a_failed_record_write_costs_only_its_own_album),
        TEST_CASE(a_failed_rewrite_drops_only_the_damaged_record),
        TEST_CASE(the_previous_covers_stay_readable_until_the_pass_ends),
        TEST_CASE(an_album_spread_over_many_folders_lists_a_bounded_number_of_them),
        TEST_CASE(albums_sharing_a_folder_list_it_once),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
