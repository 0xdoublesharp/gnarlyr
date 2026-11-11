#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "personal_tags.h"

static PersonalTags tags;
static FixtureLibrary fixture;

static void finish(void)
{
    for (int step = 0; step < 1000000 && tags.result == STORE_WORKING; step++) {
        personal_tags_step(&tags);
    }
}

static void reset(void)
{
    fake_hal_reset();
    CHECK_EQUAL(STORE_EMPTY, personal_tags_init(&tags, fixture_library_open(&fixture)));
    CHECK(sizeof tags <= 4096);
}

static void change(PersonalTagChange edit)
{
    CHECK(personal_tags_change(&tags, edit));
    finish();
    CHECK_EQUAL(STORE_DONE, tags.result);
}

static void create(uint8_t tag, const char *name)
{
    change((PersonalTagChange){.operation = PERSONAL_TAG_CREATE, .tag = tag, .name = name});
}

static void assign(uint16_t track, uint32_t mask)
{
    change((PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = track, .mask = mask});
}

static uint32_t mask(uint16_t track)
{
    uint32_t value = UINT32_MAX;
    CHECK(personal_tags_mask(&tags, track, &value));
    return value;
}

static void reopen(void)
{
    personal_tags_init(&tags, &fixture.library);
    finish();
    CHECK_EQUAL(STORE_READY, tags.load);
    CHECK_EQUAL(STORE_DONE, tags.result);
}

static void labels_and_multiple_assignments_persist_without_touching_music(void)
{
    reset();
    create(0, "Night drive");
    create(31, "Björk");
    assign(0, UINT32_C(0x80000001));
    assign(1, 1);
    reopen();
    CHECK(strcmp(personal_tags_name(&tags, 0), "Night drive") == 0);
    CHECK(strcmp(personal_tags_name(&tags, 31), "Björk") == 0);
    CHECK_EQUAL(UINT32_C(0x80000001), mask(0));
    CHECK_EQUAL(1, mask(1));
    CHECK_EQUAL(0, mask(2));
    bool matches = false;
    CHECK(personal_tags_matches(&tags, 0, UINT32_C(0x80000001), &matches));
    CHECK(matches);
    CHECK(personal_tags_matches(&tags, 1, UINT32_C(0x80000001), &matches));
    CHECK(!matches);
    CHECK_EQUAL(0, fake_hal_library_writes());
    CHECK_EQUAL(0, fake_hal_covers_writes());
}

static void rename_delete_and_slot_reuse_never_relabel_old_tracks(void)
{
    reset();
    create(0, "Sleep");
    create(1, "Walk");
    assign(0, 1);
    assign(1, 3);
    change((PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .tag = 0, .name = "Rest"});
    CHECK_EQUAL(1, mask(0));
    change((PersonalTagChange){.operation = PERSONAL_TAG_DELETE, .tag = 0});
    CHECK_EQUAL(1, tags.rows);
    CHECK_EQUAL(0, mask(0));
    CHECK_EQUAL(2, mask(1));
    create(0, "Run");
    reopen();
    CHECK_EQUAL(0, mask(0));
    CHECK_EQUAL(2, mask(1));
    CHECK(strcmp(personal_tags_name(&tags, 0), "Run") == 0);
    assign(1, 0);
    CHECK_EQUAL(0, tags.rows);
}

static void labels_require_complete_nonempty_utf8_with_at_most_32_bytes(void)
{
    reset();
    static const char *invalid[] = {"",
                                    "   ",
                                    "bad\nname",
                                    "\xc0\xaf",
                                    "\xed\xa0\x80",
                                    "\xf4\x90\x80\x80",
                                    "\xe2\x82",
                                    "123456789012345678901234567890123"};
    for (uint32_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        CHECK(!personal_tags_change(&tags, (PersonalTagChange){.operation = PERSONAL_TAG_CREATE,
                                                               .tag = 0,
                                                               .name = invalid[index]}));
    }
    create(0, "12345678901234567890123456789012");
    CHECK(!personal_tags_change(
        &tags,
        (PersonalTagChange){.operation = PERSONAL_TAG_CREATE, .tag = 0, .name = "Duplicate slot"}));
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = 0, .mask = 2}));
}

static void invalid_label_operations_preserve_existing_names_and_assignments(void)
{
    reset();
    create(0, "Ambient");
    create(1, "Focus");
    assign(0, 3);
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_CREATE, .tag = 2, .name = "Ambient"}));
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .tag = 1, .name = "Ambient"}));
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .tag = 2, .name = "New"}));
    CHECK(!personal_tags_change(&tags,
                                (PersonalTagChange){.operation = PERSONAL_TAG_DELETE, .tag = 2}));
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_CREATE, .tag = 32, .name = "New"}));
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_CREATE, .tag = 2, .name = NULL}));
    CHECK(!personal_tags_change(&tags, (PersonalTagChange){.operation = (PersonalTagOperation)99}));
    CHECK(!personal_tags_change(
        &tags,
        (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = UINT16_MAX, .mask = 1}));
    CHECK(personal_tags_name(&tags, 32) == NULL);
    reopen();
    CHECK_EQUAL(0, strcmp("Ambient", personal_tags_name(&tags, 0)));
    CHECK_EQUAL(0, strcmp("Focus", personal_tags_name(&tags, 1)));
    CHECK_EQUAL(3u, mask(0));
}

static void cancellation_and_failed_writes_keep_published_labels_and_assignments(void)
{
    for (int cutoff = 0; cutoff < 24; cutoff++) {
        reset();
        create(0, "Before");
        assign(0, 1);
        CHECK(personal_tags_change(
            &tags,
            (PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .tag = 0, .name = "After"}));
        for (int step = 0; step < cutoff && tags.result == STORE_WORKING; step++) {
            personal_tags_step(&tags);
        }
        bool committed = tags.result == STORE_DONE;
        personal_tags_cancel(&tags);
        reopen();
        CHECK(strcmp(personal_tags_name(&tags, 0), committed ? "After" : "Before") == 0);
        CHECK_EQUAL(1, mask(0));
    }
    fake_hal_fail_music_writes(true);
    CHECK(personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = 0, .mask = 0}));
    finish();
    CHECK_EQUAL(STORE_FAILED, tags.result);
    fake_hal_fail_music_writes(false);
    reopen();
    CHECK_EQUAL(1, mask(0));
}

static void paths_and_cue_ranges_survive_library_row_reordering(void)
{
    reset();
    create(0, "Favorite mood");
    assign(0, 1);
    uint32_t first = fixture.library.sections[LIBRARY_TRACKS].offset;
    uint32_t stride = fixture.library.sections[LIBRARY_TRACKS].record_size;
    uint8_t row[128];
    CHECK(stride <= sizeof row);
    memcpy(row, fixture.bytes + first, stride);
    memcpy(fixture.bytes + first, fixture.bytes + first + stride, stride);
    memcpy(fixture.bytes + first + stride, row, stride);
    CHECK(fixture_library_identify(&fixture.library));
    personal_tags_attach(&tags, &fixture.library);
    CHECK_EQUAL(0, mask(0));
    CHECK_EQUAL(1, mask(1));
    reset();
    fixture_cue_library_open(&fixture);
    personal_tags_attach(&tags, &fixture.library);
    create(0, "Cue");
    assign(0, 1);
    CHECK_EQUAL(1, mask(0));
    CHECK_EQUAL(0, mask(1));
}

static void unreadable_or_truncated_track_paths_fail_instead_of_matching(void)
{
    reset();
    create(0, "Quiet");
    assign(0, 1);
    fixture.memory.size = 0;
    uint32_t value = UINT32_MAX;
    CHECK(!personal_tags_mask(&tags, 0, &value));
    CHECK_EQUAL(0, value);
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = 0, .mask = 1}));
    fixture.memory.size = fixture.size;
    LibraryTrack row;
    CHECK(library_read_track(&fixture.library, 0, &row));
    uint32_t offset = fixture.library.sections[LIBRARY_STRINGS].offset + row.path;
    memset(fixture.bytes + offset, 'x', MIX_PATH_BYTES);
    CHECK(!personal_tags_mask(&tags, 0, &value));
    CHECK_EQUAL(0, value);
}

static void a_corrupt_newest_bank_falls_back_to_complete_previous_tags(void)
{
    reset();
    create(0, "Before");
    assign(0, 1);
    uint8_t broken = 0xff;
    const char *path = tags.active_bank == 0 ? PERSONAL_TAG_PATH_A : PERSONAL_TAG_PATH_B;
    CHECK(hal_file_write_at(path, STORE_BLOCK_BYTES + PERSONAL_TAG_CATALOG_BYTES, &broken, 1));
    reopen();
    CHECK_EQUAL(0, mask(0));
    CHECK(strcmp(personal_tags_name(&tags, 0), "Before") == 0);
}

static void replace_track_path(uint16_t track, const char *path)
{
    uint32_t length = (uint32_t)strlen(path) + 1;
    uint32_t strings = fixture.library.sections[LIBRARY_STRINGS].offset;
    uint32_t row = fixture.library.sections[LIBRARY_TRACKS].offset +
                   (uint32_t)track * fixture.library.sections[LIBRARY_TRACKS].record_size;
    CHECK(fixture.size + length <= sizeof fixture.bytes);
    store_put32(fixture.bytes + row + 4, fixture.size - strings);
    memcpy(fixture.bytes + fixture.size, path, length);
    fixture.size += length;
    fixture.memory.size = fixture.size;
    fixture.library.sections[LIBRARY_STRINGS].count = fixture.size - strings;
}

static void hash_collisions_still_require_the_complete_exact_identity(void)
{
    reset();
    create(0, "Quiet");
    create(1, "Loud");
    replace_track_path(0, "Music/key-8edd8e22536019db.flac");
    replace_track_path(1, "Music/key-03eebfcac06d29ed.flac");
    MixIdentity first;
    MixIdentity second;
    CHECK_EQUAL(STORE_READY, mix_identity_read(&fixture.library, 0, &first));
    CHECK_EQUAL(STORE_READY, mix_identity_read(&fixture.library, 1, &second));
    CHECK_EQUAL(mix_identity_hash(&first), mix_identity_hash(&second));
    assign(0, 1);
    assign(1, 2);
    reopen();
    CHECK_EQUAL(1, mask(0));
    CHECK_EQUAL(2, mask(1));
    assign(0, 0);
    CHECK_EQUAL(0, mask(0));
    CHECK_EQUAL(2, mask(1));
}

static void all_32_labels_can_be_named_and_assigned_together(void)
{
    reset();
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        char name[PERSONAL_TAG_NAME_BYTES];
        snprintf(name, sizeof name, "Tag %02u", tag);
        create(tag, name);
    }
    assign(0, UINT32_MAX);
    reopen();
    CHECK_EQUAL(UINT32_MAX, tags.catalog.active);
    CHECK_EQUAL(UINT32_MAX, mask(0));
    CHECK(strcmp(personal_tags_name(&tags, 31), "Tag 31") == 0);
    CHECK(!personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .tag = 1, .name = "Tag 00"}));
}

enum {
    DISK_BYTES = STORE_BLOCK_BYTES + PERSONAL_TAG_CATALOG_BYTES +
        PERSONAL_TAG_TRACK_LIMIT * PERSONAL_TAG_ROW_BYTES
};

typedef struct TagDiskBank {
    uint8_t bytes[DISK_BYTES];
    uint32_t size;
    uint32_t reads;
    uint32_t writes;
    uint32_t cut_write;
    uint32_t cut_bytes;
    bool blind;
} TagDiskBank;

static TagDiskBank disk[2];

static bool disk_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    TagDiskBank *bank = context;
    bank->reads++;
    if (bank->blind || offset > bank->size || length > bank->size - offset) {
        return false;
    }
    memcpy(bytes, bank->bytes + offset, length);
    return true;
}

static bool disk_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    TagDiskBank *bank = context;
    bank->writes++;
    if (offset > bank->size || length > bank->size - offset) {
        return false;
    }
    bool cut = bank->cut_write != 0 && bank->writes >= bank->cut_write;
    uint32_t kept = cut && length > bank->cut_bytes ? bank->cut_bytes : length;
    memcpy(bank->bytes + offset, bytes, kept);
    return !cut;
}

static StoreFile disk_file(TagDiskBank *bank)
{
    return (StoreFile){.source = {disk_read, bank}, .write = disk_write, .size = bank->size};
}

static bool disk_prepare(void *context, uint8_t bank, uint32_t size, StoreFile *file)
{
    TagDiskBank *banks = context;
    if (size > DISK_BYTES) {
        return false;
    }
    if (banks[bank].size < size) {
        memset(banks[bank].bytes + banks[bank].size, 0, size - banks[bank].size);
        banks[bank].size = size;
    }
    *file = disk_file(&banks[bank]);
    return true;
}

static void disk_open(void)
{
    personal_tags_open(&tags, &fixture.library,
                       (PersonalTagStorage){.files = {disk_file(&disk[0]), disk_file(&disk[1])},
                                            .prepare = disk_prepare,
                                            .context = disk});
    finish();
}

static void disk_reset(void)
{
    reset();
    for (uint8_t bank = 0; bank < 2; bank++) {
        memset(disk[bank].bytes, 0, STORE_BLOCK_BYTES);
        disk[bank].size = 0;
        disk[bank].reads = 0;
        disk[bank].writes = 0;
        disk[bank].cut_write = 0;
        disk[bank].blind = false;
    }
    disk_open();
}

static int row_compare(const void *first, const void *second)
{
    const uint8_t *a = first;
    const uint8_t *b = second;
    uint32_t ah = store_get32(a);
    uint32_t bh = store_get32(b);
    if (ah != bh) {
        return ah < bh ? -1 : 1;
    }
    MixIdentity ai;
    MixIdentity bi;
    mix_identity_decode(a + 8, MIX_RECORD_MAX, &ai);
    mix_identity_decode(b + 8, MIX_RECORD_MAX, &bi);
    int path = strcmp(ai.path, bi.path);
    if (path != 0) {
        return path;
    }
    return ai.start_ms == bi.start_ms ? 0 : ai.start_ms < bi.start_ms ? -1 : 1;
}

static void seed_full_capacity(void)
{
    TagDiskBank *bank = &disk[0];
    bank->size = DISK_BYTES;
    uint8_t *prefix = bank->bytes + STORE_BLOCK_BYTES;
    memset(prefix, 0, PERSONAL_TAG_CATALOG_BYTES);
    store_put32(prefix, 1);
    memcpy(prefix + 4, "Seed", 5);
    uint8_t *rows = prefix + PERSONAL_TAG_CATALOG_BYTES;
    for (uint32_t index = 0; index < PERSONAL_TAG_TRACK_LIMIT; index++) {
        uint8_t *row = rows + index * PERSONAL_TAG_ROW_BYTES;
        memset(row, 0, PERSONAL_TAG_ROW_BYTES);
        MixIdentity identity = {0};
        snprintf(identity.path, sizeof identity.path, "Music/tag-%05u.flac", index);
        if (index == 0) {
            CHECK_EQUAL(STORE_READY, mix_identity_read(&fixture.library, 0, &identity));
        }
        store_put32(row, mix_identity_hash(&identity));
        store_put32(row + 4, 1);
        CHECK(mix_identity_encode(&identity, row + 8) != 0);
    }
    qsort(rows, PERSONAL_TAG_TRACK_LIMIT, PERSONAL_TAG_ROW_BYTES, row_compare);
    static const StoreKind kind = {.magic = {'D', 'S', 'T', 'G'}, .version = 1};
    StoreHeader header = {.generation = 1,
                          .count = PERSONAL_TAG_TRACK_LIMIT,
                          .bytes = DISK_BYTES - STORE_BLOCK_BYTES,
                          .hash = hash_update(HASH_START, prefix, DISK_BYTES - STORE_BLOCK_BYTES)};
    store_encode(&kind, &header, bank->bytes);
}

static void all_65535_assignments_are_file_backed_and_searches_are_logarithmic(void)
{
    disk_reset();
    seed_full_capacity();
    disk_open();
    CHECK_EQUAL(STORE_READY, tags.load);
    CHECK_EQUAL(PERSONAL_TAG_TRACK_LIMIT, tags.rows);
    uint32_t before = disk[tags.active_bank].reads;
    CHECK_EQUAL(1, mask(0));
    CHECK(disk[tags.active_bank].reads - before <= 17);
    CHECK_EQUAL(0, mask(1));
    CHECK(personal_tags_change(
        &tags, (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = 1, .mask = 1}));
    personal_tags_step(&tags);
    CHECK_EQUAL(STORE_WORKING, tags.result);
    finish();
    CHECK_EQUAL(STORE_FAILED, tags.result);
    CHECK_EQUAL(PERSONAL_TAG_TRACK_LIMIT, tags.rows);
    change((PersonalTagChange){.operation = PERSONAL_TAG_DELETE, .tag = 0});
    CHECK_EQUAL(0, tags.rows);
    create(0, "Reused");
    assign(1, 1);
    disk_open();
    CHECK_EQUAL(1, tags.rows);
    CHECK_EQUAL(0, mask(0));
    CHECK_EQUAL(1, mask(1));
}

static void partial_writes_never_replace_a_complete_old_bank(void)
{
    for (uint32_t cutoff = 1; cutoff < 24; cutoff++) {
        disk_reset();
        create(0, "Before");
        assign(0, 1);
        uint8_t target = (uint8_t)(1 - tags.active_bank);
        disk[target].cut_write = disk[target].writes + cutoff;
        disk[target].cut_bytes = 7;
        CHECK(personal_tags_change(
            &tags, (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN, .track = 1, .mask = 1}));
        finish();
        bool committed = tags.result == STORE_DONE;
        disk[target].cut_write = 0;
        disk_open();
        CHECK_EQUAL(STORE_READY, tags.load);
        CHECK_EQUAL(1, mask(0));
        CHECK_EQUAL(committed ? 1 : 0, mask(1));
    }
}

static void unreadable_banks_fail_closed_and_query_errors_are_distinct_from_no_tags(void)
{
    disk_reset();
    create(0, "Before");
    assign(0, 1);
    disk[tags.active_bank].blind = true;
    uint32_t value = UINT32_MAX;
    CHECK(!personal_tags_mask(&tags, 0, &value));
    CHECK_EQUAL(0, value);
    disk_open();
    CHECK_EQUAL(STORE_UNREADABLE, tags.load);
    CHECK_EQUAL(STORE_FAILED, tags.result);
    CHECK(!personal_tags_mask(&tags, 0, &value));
    disk[0].blind = false;
    disk[1].blind = false;
    disk_open();
    CHECK_EQUAL(1, mask(0));
}

static void a_temporarily_hidden_newer_bank_cannot_be_overwritten_from_stale_tags(void)
{
    reset();
    create(0, "First");
    assign(0, 1);
    create(1, "Newest");
    const char *newest = tags.files[tags.active_bank].path;
    fake_hal_fail_file_opens(newest);
    personal_tags_init(&tags, &fixture.library);
    finish();
    fake_hal_fail_file_opens(NULL);
    CHECK(personal_tags_change(
        &tags,
        (PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .tag = 0, .name = "Stale edit"}));
    finish();
    CHECK_EQUAL(STORE_FAILED, tags.result);
    reopen();
    const char *kept = personal_tags_name(&tags, 1);
    CHECK(kept != NULL);
    if (kept != NULL) {
        CHECK(strcmp(kept, "Newest") == 0);
    }
    CHECK_EQUAL(1, mask(0));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_temporarily_hidden_newer_bank_cannot_be_overwritten_from_stale_tags),
        TEST_CASE(labels_and_multiple_assignments_persist_without_touching_music),
        TEST_CASE(rename_delete_and_slot_reuse_never_relabel_old_tracks),
        TEST_CASE(labels_require_complete_nonempty_utf8_with_at_most_32_bytes),
        TEST_CASE(invalid_label_operations_preserve_existing_names_and_assignments),
        TEST_CASE(cancellation_and_failed_writes_keep_published_labels_and_assignments),
        TEST_CASE(paths_and_cue_ranges_survive_library_row_reordering),
        TEST_CASE(unreadable_or_truncated_track_paths_fail_instead_of_matching),
        TEST_CASE(a_corrupt_newest_bank_falls_back_to_complete_previous_tags),
        TEST_CASE(hash_collisions_still_require_the_complete_exact_identity),
        TEST_CASE(all_32_labels_can_be_named_and_assigned_together),
        TEST_CASE(all_65535_assignments_are_file_backed_and_searches_are_logarithmic),
        TEST_CASE(partial_writes_never_replace_a_complete_old_bank),
        TEST_CASE(unreadable_banks_fail_closed_and_query_errors_are_distinct_from_no_tags),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
