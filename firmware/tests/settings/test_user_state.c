#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "settings/settings.h"
#include "settings/user_state.h"

enum {
    FILE_SLOTS = 96,
    CONTENT_BYTES = 600,
    BUFFER_BYTES = 64
};

typedef struct FakeFile {
    bool used;
    uint8_t volume;
    char path[USER_STATE_PATH_CAPACITY];
    uint32_t size;
    uint8_t bytes[CONTENT_BYTES];
} FakeFile;

static FakeFile files[FILE_SLOTS];
static uint8_t buffer[BUFFER_BYTES];
static const char *refused;
static int creates;

static FakeFile *find(uint8_t volume, const char *path)
{
    for (int index = 0; index < FILE_SLOTS; index++) {
        if (files[index].used && files[index].volume == volume &&
            strcmp(files[index].path, path) == 0) {
            return &files[index];
        }
    }
    return NULL;
}

static FakeFile *put(uint8_t volume, const char *path, uint32_t size)
{
    FakeFile *file = find(volume, path);
    for (int index = 0; file == NULL && index < FILE_SLOTS; index++) {
        file = files[index].used ? NULL : &files[index];
    }
    memset(file, 0, sizeof *file);
    file->used = true;
    file->volume = volume;
    snprintf(file->path, sizeof file->path, "%s", path);
    file->size = size;
    return file;
}

static uint32_t fake_size(void *context, const UserStatePlace *place)
{
    (void)context;
    FakeFile *file = find(place->volume, place->path);
    return file != NULL ? file->size : USER_STATE_MISSING;
}

static bool fake_read(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    (void)context;
    FakeFile *file = find(place->volume, place->path);
    if (file == NULL || chunk.offset + chunk.length > file->size ||
        chunk.offset + chunk.length > CONTENT_BYTES) {
        return false;
    }
    memcpy(chunk.bytes, file->bytes + chunk.offset, chunk.length);
    return true;
}

static bool fake_create(void *context, const UserStatePlace *place, uint32_t size)
{
    (void)context;
    creates++;
    FakeFile *file = put(place->volume, place->path, size);
    memset(file->bytes, 0xA5, sizeof file->bytes);
    return true;
}

static bool fake_write(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    (void)context;
    FakeFile *file = find(place->volume, place->path);
    if (file == NULL || chunk.offset + chunk.length > file->size ||
        (refused != NULL && strstr(place->path, refused) != NULL)) {
        return false;
    }
    memcpy(file->bytes + chunk.offset, chunk.bytes, chunk.length);
    return true;
}

static bool fake_remove(void *context, const UserStatePlace *place)
{
    (void)context;
    FakeFile *file = find(place->volume, place->path);
    if (file != NULL) {
        file->used = false;
    }
    return file != NULL;
}

static const UserStateIo IO = {fake_size,   fake_read, fake_create, fake_write,
                               fake_remove, NULL,      buffer,      BUFFER_BYTES};

static void reset(void)
{
    memset(files, 0, sizeof files);
    refused = NULL;
    creates = 0;
}

static void path_of(char *path, const char *folder, const char *name)
{
    CHECK(user_state_path(path, folder, name));
}

static FakeFile *put_data(uint8_t volume, const char *folder, const char *name, uint32_t size)
{
    char path[USER_STATE_PATH_CAPACITY];
    path_of(path, folder, name);
    FakeFile *file = put(volume, path, size);
    for (uint32_t index = 0; index < size && index < CONTENT_BYTES; index++) {
        file->bytes[index] = (uint8_t)(index * 7 + (uint8_t)name[0]);
    }
    return file;
}

static FakeFile *put_store(uint8_t volume, const char *folder, const char *name, const char *magic,
                           uint16_t version)
{
    FakeFile *file = put_data(volume, folder, name, 520);
    memcpy(file->bytes, magic, 4);
    file->bytes[4] = (uint8_t)version;
    file->bytes[5] = (uint8_t)(version >> 8);
    return file;
}

static FakeFile *find_data(uint8_t volume, const char *folder, const char *name)
{
    char path[USER_STATE_PATH_CAPACITY];
    path_of(path, folder, name);
    return find(volume, path);
}

static bool same_file(const FakeFile *first, const FakeFile *second)
{
    return first != NULL && second != NULL && first->size == second->size &&
           memcmp(first->bytes, second->bytes, first->size) == 0;
}

static UserStateList read_list(void)
{
    FakeFile *file = find(USER_STATE_INTERNAL, USER_STATE_LIST_PATH);
    uint8_t zero[USER_STATE_LIST_BYTES] = {0};
    return user_state_list_decode(file != NULL ? file->bytes : zero);
}

static void settings_file(const char *folder, uint8_t version)
{
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings settings = settings_defaults();
    settings_encode(&settings, bytes);
    bytes[4] = version;
    FakeFile *file = put_data(USER_STATE_INTERNAL, folder, "settings.bin", sizeof bytes);
    memcpy(file->bytes, bytes, sizeof bytes);
}

static void the_catalogue_names_every_user_file_once(void)
{
    char names[200][USER_STATE_NAME_CAPACITY];
    uint32_t count = user_state_count();
    CHECK(count <= 200);
    for (uint32_t index = 0; index < count; index++) {
        user_state_kind(index, names[index]);
        CHECK(strlen(names[index]) < USER_STATE_NAME_CAPACITY);
        for (uint32_t earlier = 0; earlier < index; earlier++) {
            CHECK(strcmp(names[earlier], names[index]) != 0);
        }
        CHECK(strcmp(names[index], "library.db") != 0);
        CHECK(strcmp(names[index], "covers.bin") != 0);
    }
    CHECK_EQUAL(0, strcmp(names[count - 2 * USER_STATE_TAPES], "mix00a.bin"));
    CHECK_EQUAL(0, strcmp(names[count - 1], "mix31b.bin"));
    char path[USER_STATE_PATH_CAPACITY];
    CHECK(user_state_path(path, "backup/2/", "favorite_added.bin"));
    CHECK_EQUAL(0, strcmp(path, "/" FIRMWARE_DATA_DIRECTORY "/backup/2/favorite_added.bin"));
}

static void the_settings_entry_tracks_the_settings_format_version(void)
{
    char name[USER_STATE_NAME_CAPACITY];
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings settings = settings_defaults();
    settings_encode(&settings, bytes);
    UserStateKind kind = user_state_kind(0, name);
    CHECK_EQUAL(0, strcmp(name, "settings.bin"));
    CHECK_EQUAL(FORMAT_AGE_CURRENT, user_state_age(&kind, bytes, sizeof bytes));
    bytes[4]++;
    CHECK_EQUAL(FORMAT_AGE_NEWER, user_state_age(&kind, bytes, sizeof bytes));
    bytes[0] = 'X';
    CHECK_EQUAL(FORMAT_AGE_INVALID, user_state_age(&kind, bytes, sizeof bytes));
}

static void a_copy_moves_every_byte_through_a_small_buffer_and_checks_it(void)
{
    reset();
    FakeFile *source = put_data(USER_STATE_MUSIC, "", "listens.bin", 517);
    UserStatePlace from = {USER_STATE_MUSIC, source->path};
    UserStatePlace to = {USER_STATE_INTERNAL, "/copy"};
    CHECK(user_state_copy(&IO, &from, &to));
    CHECK(same_file(source, find(USER_STATE_INTERNAL, "/copy")));
    refused = "/copy";
    CHECK(!user_state_copy(&IO, &from, &to));
    UserStatePlace missing = {USER_STATE_MUSIC, "/none"};
    CHECK(!user_state_copy(&IO, &missing, &to));
}

static void a_backup_copies_the_small_user_files_and_lists_the_old_build(void)
{
    reset();
    settings_file("", 1);
    FakeFile *keys = put_data(USER_STATE_MUSIC, "", "track_keys.bin", 300);
    FakeFile *tape = put_data(USER_STATE_MUSIC, "", "mix07b.bin", 90);
    put_data(USER_STATE_MUSIC, "", "library.db", 500);
    put(USER_STATE_MUSIC, "/" FIRMWARE_DATA_DIRECTORY "/listens.bin", USER_STATE_FILE_LIMIT + 1);
    CHECK(user_state_backup(&IO, "1.0.0-aaaa1111"));
    CHECK(same_file(find_data(USER_STATE_INTERNAL, "", "settings.bin"),
                    find_data(USER_STATE_INTERNAL, "backup/1/", "settings.bin")));
    CHECK(same_file(keys, find_data(USER_STATE_INTERNAL, "backup/1/", "track_keys.bin")));
    CHECK(same_file(tape, find_data(USER_STATE_INTERNAL, "backup/1/", "mix07b.bin")));
    CHECK(find_data(USER_STATE_INTERNAL, "backup/1/", "library.db") == NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "backup/1/", "listens.bin") == NULL);
    UserStateList list = read_list();
    CHECK_EQUAL(0, strcmp(list.builds[0], "1.0.0-aaaa1111"));
    CHECK_EQUAL(0, list.builds[1][0]);
    CHECK_EQUAL(0, list.newest);
    CHECK_EQUAL(0, user_state_list_order(&list, 0));
    CHECK_EQUAL(-1, user_state_list_order(&list, 1));
}

static void the_last_two_backups_are_kept_and_the_oldest_is_replaced(void)
{
    reset();
    put_data(USER_STATE_INTERNAL, "", "buttons.bin", 32);
    put_data(USER_STATE_MUSIC, "", "moments.bin", 40);
    CHECK(user_state_backup(&IO, "build-one"));
    FakeFile *moments = find_data(USER_STATE_MUSIC, "", "moments.bin");
    moments->used = false;
    CHECK(user_state_backup(&IO, "build-two"));
    UserStateList list = read_list();
    CHECK_EQUAL(1, list.newest);
    CHECK_EQUAL(1, user_state_list_order(&list, 0));
    CHECK_EQUAL(0, user_state_list_order(&list, 1));
    CHECK(find_data(USER_STATE_INTERNAL, "backup/1/", "moments.bin") != NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "backup/2/", "moments.bin") == NULL);
    CHECK(user_state_backup(&IO, "build-three"));
    list = read_list();
    CHECK_EQUAL(0, strcmp(list.builds[0], "build-three"));
    CHECK_EQUAL(0, strcmp(list.builds[1], "build-two"));
    CHECK_EQUAL(0, list.newest);
    CHECK(find_data(USER_STATE_INTERNAL, "backup/1/", "moments.bin") == NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "backup/1/", "buttons.bin") != NULL);
}

static void a_second_backup_of_the_same_build_reuses_its_slot(void)
{
    reset();
    put_data(USER_STATE_INTERNAL, "", "buttons.bin", 32);
    CHECK(user_state_backup(&IO, "build-one"));
    CHECK(user_state_backup(&IO, "build-two"));
    CHECK(user_state_backup(&IO, "build-two"));
    UserStateList list = read_list();
    CHECK_EQUAL(0, strcmp(list.builds[0], "build-one"));
    CHECK_EQUAL(0, strcmp(list.builds[1], "build-two"));
    CHECK_EQUAL(1, list.newest);
}

static void an_unfinished_backup_is_not_listed(void)
{
    reset();
    put_data(USER_STATE_INTERNAL, "", "buttons.bin", 32);
    CHECK(user_state_backup(&IO, "build-one"));
    refused = "backup/2/";
    CHECK(!user_state_backup(&IO, "build-two"));
    UserStateList list = read_list();
    CHECK_EQUAL(0, strcmp(list.builds[0], "build-one"));
    CHECK_EQUAL(0, list.builds[1][0]);
    CHECK_EQUAL(0, user_state_list_order(&list, 0));
    CHECK_EQUAL(-1, user_state_list_order(&list, 1));
}

static void a_damaged_list_reads_as_no_backups(void)
{
    UserStateList list = {.newest = 1};
    snprintf(list.builds[1], sizeof list.builds[1], "build-two");
    uint8_t bytes[USER_STATE_LIST_BYTES];
    user_state_list_encode(&list, bytes);
    CHECK_EQUAL(0, strcmp(user_state_list_decode(bytes).builds[1], "build-two"));
    bytes[10] ^= 1;
    UserStateList damaged = user_state_list_decode(bytes);
    CHECK_EQUAL(USER_STATE_NO_SLOT, damaged.newest);
    CHECK_EQUAL(-1, user_state_list_order(&damaged, 0));
}

static void a_restore_puts_the_backed_up_files_back_and_leaves_the_rest(void)
{
    reset();
    settings_file("", 1);
    put_data(USER_STATE_MUSIC, "", "favorites.bin", 70);
    CHECK(user_state_backup(&IO, "build-one"));
    FakeFile saved = *find_data(USER_STATE_INTERNAL, "", "settings.bin");
    find_data(USER_STATE_INTERNAL, "", "settings.bin")->bytes[6] = 2;
    find_data(USER_STATE_MUSIC, "", "favorites.bin")->used = false;
    FakeFile *later = put_data(USER_STATE_MUSIC, "", "bookmarks.bin", 30);
    CHECK(user_state_restore(&IO, 0));
    CHECK(same_file(&saved, find_data(USER_STATE_INTERNAL, "", "settings.bin")));
    CHECK(find_data(USER_STATE_MUSIC, "", "favorites.bin") != NULL);
    CHECK(find_data(USER_STATE_MUSIC, "", "bookmarks.bin") == later);
    CHECK(!user_state_restore(&IO, 1));
    CHECK(!user_state_restore(&IO, USER_STATE_BACKUPS));
}

static void a_newer_file_is_set_aside_so_the_older_build_starts_fresh(void)
{
    reset();
    settings_file("", 2);
    FakeFile newer_settings = *find_data(USER_STATE_INTERNAL, "", "settings.bin");
    FakeFile *journal = put_store(USER_STATE_MUSIC, "", "session.bin", "DSSJ", 2);
    FakeFile newer_journal = *journal;
    put_store(USER_STATE_MUSIC, "", "order.bin", "DSTO", 1);
    put_data(USER_STATE_INTERNAL, "", "eq_slots.bin", 40);
    user_state_settle(&IO);
    CHECK(find_data(USER_STATE_INTERNAL, "", "settings.bin") == NULL);
    CHECK(find_data(USER_STATE_MUSIC, "", "session.bin") == NULL);
    CHECK(same_file(&newer_settings, find_data(USER_STATE_INTERNAL, "newer/", "settings.bin")));
    CHECK(same_file(&newer_journal, find_data(USER_STATE_INTERNAL, "newer/", "session.bin")));
    CHECK(find_data(USER_STATE_MUSIC, "", "order.bin") != NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "order.bin") == NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "", "eq_slots.bin") != NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "eq_slots.bin") == NULL);
}

static void a_newer_file_that_cannot_be_set_aside_stays_where_it_is(void)
{
    reset();
    settings_file("", 2);
    FakeFile newer = *find_data(USER_STATE_INTERNAL, "", "settings.bin");
    refused = "newer/";
    user_state_settle(&IO);
    CHECK(same_file(&newer, find_data(USER_STATE_INTERNAL, "", "settings.bin")));
}

static void a_set_aside_file_returns_when_a_build_can_read_it_again(void)
{
    reset();
    settings_file("newer/", 1);
    FakeFile kept = *find_data(USER_STATE_INTERNAL, "newer/", "settings.bin");
    settings_file("", 1);
    find_data(USER_STATE_INTERNAL, "", "settings.bin")->bytes[6] = 9;
    put_store(USER_STATE_INTERNAL, "newer/", "session.bin", "DSSJ", 3);
    user_state_settle(&IO);
    CHECK(same_file(&kept, find_data(USER_STATE_INTERNAL, "", "settings.bin")));
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "settings.bin") == NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "session.bin") != NULL);
    CHECK(find_data(USER_STATE_MUSIC, "", "session.bin") == NULL);
}

static void a_downgrade_and_return_loses_nothing(void)
{
    reset();
    settings_file("", 2);
    FakeFile written_by_v2 = *find_data(USER_STATE_INTERNAL, "", "settings.bin");
    user_state_settle(&IO);
    settings_file("", 1);
    user_state_settle(&IO);
    CHECK_EQUAL(1, find_data(USER_STATE_INTERNAL, "", "settings.bin")->bytes[4]);
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "settings.bin") != NULL);
    FakeFile *aside = find_data(USER_STATE_INTERNAL, "newer/", "settings.bin");
    aside->bytes[4] = 1;
    user_state_settle(&IO);
    written_by_v2.bytes[4] = 1;
    CHECK(same_file(&written_by_v2, find_data(USER_STATE_INTERNAL, "", "settings.bin")));
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "settings.bin") == NULL);
}

static void the_mark_carries_the_build_and_a_restore_request(void)
{
    uint8_t mark[USER_STATE_MARK_BYTES];
    uint8_t again[USER_STATE_MARK_BYTES];
    user_state_mark("1.0.0-aaaa1111", 0, mark);
    user_state_mark("1.0.0-aaaa1111", 0, again);
    CHECK_EQUAL(0, memcmp(mark, again, sizeof mark));
    CHECK_EQUAL(hash_update(HASH_START, "1.0.0-aaaa1111", 14), hash_load(mark));
    CHECK_EQUAL(0, mark[USER_STATE_MARK_REQUEST_AT]);
    CHECK_EQUAL(0, user_state_mark_request(mark));
    user_state_mark("1.0.0-bbbb2222", 0, again);
    CHECK(memcmp(mark, again, sizeof mark) != 0);
    mark[USER_STATE_MARK_REQUEST_AT] = 2;
    CHECK_EQUAL(2, user_state_mark_request(mark));
    mark[USER_STATE_MARK_REQUEST_AT] = 3;
    CHECK_EQUAL(0, user_state_mark_request(mark));
    mark[USER_STATE_MARK_REQUEST_AT] = 1;
    mark[9] ^= 1;
    CHECK_EQUAL(0, user_state_mark_request(mark));
    CHECK_EQUAL(user_state_build_hash("0123456789abcdef"),
                user_state_build_hash("0123456789abcdefXYZ"));
}

static void a_run_restores_on_request_settles_and_records_the_build(void)
{
    reset();
    settings_file("", 1);
    CHECK(user_state_backup(&IO, "build-one"));
    FakeFile saved = *find_data(USER_STATE_INTERNAL, "", "settings.bin");
    find_data(USER_STATE_INTERNAL, "", "settings.bin")->bytes[6] = 2;
    put_store(USER_STATE_MUSIC, "", "recap.bin", "DSRC", 5);
    FakeFile *mark = put(USER_STATE_INTERNAL, USER_STATE_MARK_PATH, USER_STATE_MARK_BYTES);
    user_state_mark("build-two", 1, mark->bytes);
    CHECK(user_state_run(&IO, "build-two", false));
    CHECK(same_file(&saved, find_data(USER_STATE_INTERNAL, "", "settings.bin")));
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "recap.bin") != NULL);
    uint8_t expected[USER_STATE_MARK_BYTES];
    user_state_mark("build-two", 0, expected);
    CHECK_EQUAL(0, memcmp(expected, find(USER_STATE_INTERNAL, USER_STATE_MARK_PATH)->bytes,
                          sizeof expected));
}

static void a_new_build_backs_up_what_the_previous_build_left(void)
{
    reset();
    FakeFile *mark = put(USER_STATE_INTERNAL, USER_STATE_MARK_PATH, USER_STATE_MARK_BYTES);
    user_state_mark("1.0.0-aaaa1111", 0, mark->bytes);
    settings_file("", 1);
    FakeFile before = *find_data(USER_STATE_INTERNAL, "", "settings.bin");
    put_store(USER_STATE_MUSIC, "", "listens.bin", "DSLL", 9);
    FakeFile newer = *find_data(USER_STATE_MUSIC, "", "listens.bin");
    CHECK(user_state_run(&IO, "1.0.0-bbbb2222", true));
    UserStateList list = read_list();
    CHECK_EQUAL(0, strcmp(list.builds[0], "1.0.0-aaaa1111"));
    CHECK(same_file(&before, find_data(USER_STATE_INTERNAL, "backup/1/", "settings.bin")));
    CHECK(same_file(&newer, find_data(USER_STATE_INTERNAL, "backup/1/", "listens.bin")));
    CHECK(same_file(&newer, find_data(USER_STATE_INTERNAL, "newer/", "listens.bin")));
    CHECK(find_data(USER_STATE_MUSIC, "", "listens.bin") == NULL);
    creates = 0;
    CHECK(user_state_run(&IO, "1.0.0-bbbb2222", true));
    CHECK_EQUAL(0, creates);
    CHECK_EQUAL(0, strcmp(read_list().builds[0], "1.0.0-aaaa1111"));
    CHECK_EQUAL(0, read_list().builds[1][0]);
}

static void a_first_run_only_writes_the_mark(void)
{
    reset();
    settings_file("", 2);
    CHECK(user_state_run(&IO, "build-one", true));
    CHECK(find(USER_STATE_INTERNAL, USER_STATE_MARK_PATH) != NULL);
    CHECK(find(USER_STATE_INTERNAL, USER_STATE_LIST_PATH) == NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "", "settings.bin") != NULL);
    CHECK_EQUAL(1, creates);
}

static void a_failed_backup_keeps_the_previous_build_mark_for_a_retry(void)
{
    reset();
    FakeFile *mark = put(USER_STATE_INTERNAL, USER_STATE_MARK_PATH, USER_STATE_MARK_BYTES);
    user_state_mark("build-one", 0, mark->bytes);
    uint8_t before[USER_STATE_MARK_BYTES];
    memcpy(before, mark->bytes, sizeof before);
    settings_file("", 1);
    refused = "backup/1/settings.bin";
    CHECK(!user_state_run(&IO, "build-two", true));
    CHECK_EQUAL(0, memcmp(before, mark->bytes, sizeof before));
    refused = NULL;
    CHECK(user_state_run(&IO, "build-two", true));
    CHECK_EQUAL(0, strcmp(read_list().builds[0], "build-one"));
}

static void a_pass_without_a_new_build_or_a_request_touches_nothing(void)
{
    reset();
    settings_file("", 2);
    CHECK(user_state_run(&IO, "build-one", false));
    CHECK(find(USER_STATE_INTERNAL, USER_STATE_MARK_PATH) == NULL);
    CHECK_EQUAL(0, creates);
    FakeFile *mark = put(USER_STATE_INTERNAL, USER_STATE_MARK_PATH, USER_STATE_MARK_BYTES);
    user_state_mark("build-zero", 0, mark->bytes);
    CHECK(user_state_run(&IO, "build-one", false));
    CHECK_EQUAL(0, creates);
    CHECK(find_data(USER_STATE_INTERNAL, "", "settings.bin") != NULL);
    CHECK(find_data(USER_STATE_INTERNAL, "newer/", "settings.bin") == NULL);
}

static void a_bad_restore_request_reports_failure_and_is_cleared(void)
{
    reset();
    CHECK(user_state_run(&IO, "build-one", true));
    CHECK(find(USER_STATE_INTERNAL, USER_STATE_MARK_PATH) != NULL);
    user_state_mark("build-one", 2, find(USER_STATE_INTERNAL, USER_STATE_MARK_PATH)->bytes);
    CHECK(!user_state_run(&IO, "build-one", false));
    CHECK_EQUAL(0, user_state_mark_request(find(USER_STATE_INTERNAL, USER_STATE_MARK_PATH)->bytes));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_catalogue_names_every_user_file_once),
        TEST_CASE(the_settings_entry_tracks_the_settings_format_version),
        TEST_CASE(a_copy_moves_every_byte_through_a_small_buffer_and_checks_it),
        TEST_CASE(a_backup_copies_the_small_user_files_and_lists_the_old_build),
        TEST_CASE(the_last_two_backups_are_kept_and_the_oldest_is_replaced),
        TEST_CASE(a_second_backup_of_the_same_build_reuses_its_slot),
        TEST_CASE(an_unfinished_backup_is_not_listed),
        TEST_CASE(a_damaged_list_reads_as_no_backups),
        TEST_CASE(a_restore_puts_the_backed_up_files_back_and_leaves_the_rest),
        TEST_CASE(a_newer_file_is_set_aside_so_the_older_build_starts_fresh),
        TEST_CASE(a_newer_file_that_cannot_be_set_aside_stays_where_it_is),
        TEST_CASE(a_set_aside_file_returns_when_a_build_can_read_it_again),
        TEST_CASE(a_downgrade_and_return_loses_nothing),
        TEST_CASE(the_mark_carries_the_build_and_a_restore_request),
        TEST_CASE(a_run_restores_on_request_settles_and_records_the_build),
        TEST_CASE(a_new_build_backs_up_what_the_previous_build_left),
        TEST_CASE(a_first_run_only_writes_the_mark),
        TEST_CASE(a_failed_backup_keeps_the_previous_build_mark_for_a_retry),
        TEST_CASE(a_pass_without_a_new_build_or_a_request_touches_nothing),
        TEST_CASE(a_bad_restore_request_reports_failure_and_is_cleared),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
