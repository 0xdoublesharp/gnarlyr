#include "settings/user_state.h"

#include <stddef.h>

#include "core/hash.h"

#include "settings/settings.h"

enum {
    TAPE_DIGIT_AT = 3,
    TAPE_SIDE_AT = 5,
    MARK_VERSION = 1,
    MARK_VERSION_AT = USER_STATE_MARK_VERSION_AT,
    MARK_REQUEST_AT = USER_STATE_MARK_REQUEST_AT,
    MARK_BUILD_AT = 8
};

#define STORE_FILE(file, volume, a, b, c, d) {(file), (volume), {(a), (b), (c), (d)}, 4, 4, 2, 1}
#define PLAIN_FILE(file, volume) {(file), (volume), {0, 0, 0, 0}, 0, 0, 0, 0}

static const UserStateKind FILES[] = {
    {"settings.bin", USER_STATE_INTERNAL, {'D', 'S', 'S', 'T'}, 4, 4, 1, SETTINGS_FORMAT_VERSION},
    {"buttons.bin", USER_STATE_INTERNAL, {'D', 'S', 'B', 'T'}, 4, 4, 1, 1},
    STORE_FILE("headphones.bin", USER_STATE_INTERNAL, 'D', 'S', 'H', 'F'),
    STORE_FILE("parametric.bin", USER_STATE_INTERNAL, 'P', 'E', 'Q', 'F'),
    STORE_FILE("theme.bin", USER_STATE_INTERNAL, 'D', 'S', 'T', 'S'),
    {"discovery.bin", USER_STATE_INTERNAL, {'D', 'S', 'D', 'V'}, 4, 4, 1, 1},
    PLAIN_FILE("eq_slots.bin", USER_STATE_INTERNAL),
    {"bluetooth.bin", USER_STATE_MUSIC, {'B', 'T', 0, 0}, 2, 2, 1, 1},
    STORE_FILE("session.bin", USER_STATE_MUSIC, 'D', 'S', 'S', 'J'),
    STORE_FILE("order.bin", USER_STATE_MUSIC, 'D', 'S', 'T', 'O'),
    STORE_FILE("mixes.bin", USER_STATE_MUSIC, 'D', 'S', 'M', 'C'),
    STORE_FILE("moments.bin", USER_STATE_MUSIC, 'D', 'S', 'M', 'O'),
    STORE_FILE("listens.bin", USER_STATE_MUSIC, 'D', 'S', 'L', 'L'),
    STORE_FILE("recap.bin", USER_STATE_MUSIC, 'D', 'S', 'R', 'C'),
    STORE_FILE("track_keys.bin", USER_STATE_MUSIC, 'D', 'S', 'T', 'K'),
    STORE_FILE("favorites.bin", USER_STATE_MUSIC, 'D', 'S', 'F', 'V'),
    STORE_FILE("bookmarks.bin", USER_STATE_MUSIC, 'D', 'S', 'B', 'K'),
    STORE_FILE("tags-a.bin", USER_STATE_MUSIC, 'D', 'S', 'T', 'G'),
    STORE_FILE("tags-b.bin", USER_STATE_MUSIC, 'D', 'S', 'T', 'G'),
    PLAIN_FILE("favorite_added.bin", USER_STATE_MUSIC),
    PLAIN_FILE("hidden.bin", USER_STATE_MUSIC),
};

static const UserStateKind TAPE = STORE_FILE("mix00a.bin", USER_STATE_MUSIC, 'D', 'S', 'M', 'T');

enum {
    FILE_COUNT = sizeof FILES / sizeof FILES[0]
};

uint32_t user_state_count(void)
{
    return FILE_COUNT + 2 * USER_STATE_TAPES;
}

static void copy_text(char *target, const char *source)
{
    size_t index = 0;
    for (; source[index] != '\0'; index++) {
        target[index] = source[index];
    }
    target[index] = '\0';
}

UserStateKind user_state_kind(uint32_t index, char *name)
{
    if (index < FILE_COUNT) {
        copy_text(name, FILES[index].name);
        return FILES[index];
    }
    uint32_t tape = index - FILE_COUNT;
    copy_text(name, TAPE.name);
    name[TAPE_DIGIT_AT] = (char)('0' + tape / 2 / 10);
    name[TAPE_DIGIT_AT + 1] = (char)('0' + tape / 2 % 10);
    name[TAPE_SIDE_AT] = (char)('a' + tape % 2);
    return TAPE;
}

static bool same_bytes(const uint8_t *first, const uint8_t *second, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

FormatAge user_state_age(const UserStateKind *kind, const uint8_t *head, uint32_t length)
{
    if (kind->version_width == 0) {
        return FORMAT_AGE_CURRENT;
    }
    if (length < (uint32_t)kind->version_at + kind->version_width ||
        !same_bytes(head, kind->magic, kind->magic_length)) {
        return FORMAT_AGE_INVALID;
    }
    uint32_t version = head[kind->version_at];
    if (kind->version_width == 2) {
        version |= (uint32_t)head[kind->version_at + 1] << 8;
    }
    return format_age(kind->current, version);
}

bool user_state_path(char *path, const char *folder, const char *name)
{
    static const char ROOT[] = USER_STATE_FOLDER;
    size_t length = 0;
    const char *parts[3] = {ROOT, folder, name};
    for (int part = 0; part < 3; part++) {
        for (const char *at = parts[part]; *at != '\0'; at++) {
            if (length + 1 >= USER_STATE_PATH_CAPACITY) {
                return false;
            }
            path[length++] = *at;
        }
    }
    path[length] = '\0';
    return true;
}

static bool copy_chunk(const UserStateIo *io, const UserStatePlace *from, const UserStatePlace *to,
                       UserStateChunk chunk)
{
    uint32_t half = io->buffer_bytes / 2;
    UserStateChunk back = {chunk.offset, io->buffer + half, chunk.length};
    return io->read(io->context, from, chunk) && io->write(io->context, to, chunk) &&
           io->read(io->context, to, back) && same_bytes(chunk.bytes, back.bytes, chunk.length);
}

bool user_state_copy(const UserStateIo *io, const UserStatePlace *from, const UserStatePlace *to)
{
    uint32_t size = io->size(io->context, from);
    uint32_t half = io->buffer_bytes / 2;
    if (size == USER_STATE_MISSING || half == 0 || !io->create(io->context, to, size)) {
        return false;
    }
    for (uint32_t offset = 0; offset < size; offset += half) {
        uint32_t length = size - offset < half ? size - offset : half;
        if (!copy_chunk(io, from, to, (UserStateChunk){offset, io->buffer, length})) {
            return false;
        }
    }
    return true;
}

static UserStateList load_list(const UserStateIo *io)
{
    static const UserStatePlace PLACE = {USER_STATE_INTERNAL, USER_STATE_LIST_PATH};
    uint8_t bytes[USER_STATE_LIST_BYTES] = {0};
    if (io->size(io->context, &PLACE) == USER_STATE_LIST_BYTES) {
        (void)io->read(io->context, &PLACE, (UserStateChunk){0, bytes, sizeof bytes});
    }
    return user_state_list_decode(bytes);
}

static bool store_list(const UserStateIo *io, const UserStateList *list)
{
    static const UserStatePlace PLACE = {USER_STATE_INTERNAL, USER_STATE_LIST_PATH};
    uint8_t bytes[USER_STATE_LIST_BYTES];
    user_state_list_encode(list, bytes);
    bool sized = io->size(io->context, &PLACE) == USER_STATE_LIST_BYTES ||
                 io->create(io->context, &PLACE, USER_STATE_LIST_BYTES);
    return sized && io->write(io->context, &PLACE, (UserStateChunk){0, bytes, sizeof bytes});
}

static bool same_build(const char *stored, const char *build)
{
    for (int at = 0; at < USER_STATE_BUILD_BYTES; at++) {
        if (stored[at] != build[at]) {
            return false;
        }
        if (build[at] == '\0') {
            return true;
        }
    }
    return build[USER_STATE_BUILD_BYTES] == '\0';
}

static uint8_t backup_slot(const UserStateList *list, const char *build)
{
    for (uint8_t slot = 0; slot < USER_STATE_BACKUPS; slot++) {
        if (list->builds[slot][0] != '\0' && same_build(list->builds[slot], build)) {
            return slot;
        }
    }
    for (uint8_t slot = 0; slot < USER_STATE_BACKUPS; slot++) {
        if (list->builds[slot][0] == '\0') {
            return slot;
        }
    }
    return list->newest == USER_STATE_NO_SLOT ? 0 : (uint8_t)(1 - list->newest);
}

static const char *slot_folder(uint8_t slot)
{
    static const char *const FOLDERS[USER_STATE_BACKUPS] = {"backup/1/", "backup/2/"};
    return FOLDERS[slot];
}

typedef struct FilePair {
    char name[USER_STATE_NAME_CAPACITY];
    char source_path[USER_STATE_PATH_CAPACITY];
    char target_path[USER_STATE_PATH_CAPACITY];
    UserStatePlace source;
    UserStatePlace target;
    UserStateKind kind;
} FilePair;

static bool pair_at(FilePair *pair, uint32_t index, const char *source, const char *target)
{
    pair->kind = user_state_kind(index, pair->name);
    bool named = user_state_path(pair->source_path, source, pair->name) &&
                 user_state_path(pair->target_path, target, pair->name);
    pair->source = (UserStatePlace){source[0] == '\0' ? pair->kind.volume : USER_STATE_INTERNAL,
                                    pair->source_path};
    pair->target = (UserStatePlace){target[0] == '\0' ? pair->kind.volume : USER_STATE_INTERNAL,
                                    pair->target_path};
    return named;
}

static bool backup_file(const UserStateIo *io, const FilePair *pair)
{
    uint32_t size = io->size(io->context, &pair->source);
    if (size == USER_STATE_MISSING || size == 0 || size > USER_STATE_FILE_LIMIT) {
        return io->size(io->context, &pair->target) == USER_STATE_MISSING ||
               io->remove(io->context, &pair->target);
    }
    return user_state_copy(io, &pair->source, &pair->target);
}

static void set_build(char *stored, const char *build)
{
    int at = 0;
    for (; at < USER_STATE_BUILD_BYTES && build[at] != '\0'; at++) {
        stored[at] = build[at];
    }
    for (; at <= USER_STATE_BUILD_BYTES; at++) {
        stored[at] = '\0';
    }
}

bool user_state_backup(const UserStateIo *io, const char *build)
{
    UserStateList list = load_list(io);
    uint8_t slot = backup_slot(&list, build);
    set_build(list.builds[slot], "");
    list.newest = list.newest == slot ? USER_STATE_NO_SLOT : list.newest;
    if (!store_list(io, &list)) {
        return false;
    }
    bool copied = true;
    for (uint32_t index = 0; index < user_state_count(); index++) {
        FilePair pair;
        copied = pair_at(&pair, index, "", slot_folder(slot)) && backup_file(io, &pair) && copied;
    }
    if (!copied) {
        return false;
    }
    set_build(list.builds[slot], build);
    list.newest = slot;
    return store_list(io, &list);
}

bool user_state_restore(const UserStateIo *io, uint8_t slot)
{
    if (slot >= USER_STATE_BACKUPS || load_list(io).builds[slot][0] == '\0') {
        return false;
    }
    bool restored = true;
    for (uint32_t index = 0; index < user_state_count(); index++) {
        FilePair pair;
        if (!pair_at(&pair, index, slot_folder(slot), "")) {
            restored = false;
        } else if (io->size(io->context, &pair.source) != USER_STATE_MISSING) {
            restored = user_state_copy(io, &pair.source, &pair.target) && restored;
        }
    }
    return restored;
}

static FormatAge age_at(const UserStateIo *io, const UserStatePlace *place,
                        const UserStateKind *kind)
{
    uint8_t head[USER_STATE_HEAD_BYTES];
    uint32_t size = io->size(io->context, place);
    uint32_t length = size < sizeof head ? size : sizeof head;
    if (size == USER_STATE_MISSING ||
        !io->read(io->context, place, (UserStateChunk){0, head, length})) {
        return FORMAT_AGE_INVALID;
    }
    return user_state_age(kind, head, length);
}

static bool move(const UserStateIo *io, const UserStatePlace *from, const UserStatePlace *to)
{
    return user_state_copy(io, from, to) && io->remove(io->context, from);
}

static void settle_file(const UserStateIo *io, uint32_t index)
{
    FilePair pair;
    if (!pair_at(&pair, index, "", "newer/") || pair.kind.version_width == 0) {
        return;
    }
    if (age_at(io, &pair.source, &pair.kind) == FORMAT_AGE_NEWER) {
        move(io, &pair.source, &pair.target);
        return;
    }
    FormatAge kept = age_at(io, &pair.target, &pair.kind);
    if (kept == FORMAT_AGE_CURRENT || kept == FORMAT_AGE_OLDER) {
        move(io, &pair.target, &pair.source);
    }
}

void user_state_settle(const UserStateIo *io)
{
    for (uint32_t index = 0; index < user_state_count(); index++) {
        settle_file(io, index);
    }
}

uint32_t user_state_build_hash(const char *build)
{
    uint32_t length = 0;
    while (length < USER_STATE_BUILD_BYTES && build[length] != '\0') {
        length++;
    }
    return hash_update(HASH_START, build, length);
}

void user_state_mark(const char *build, uint8_t request, uint8_t *bytes)
{
    for (int index = 0; index < USER_STATE_MARK_BYTES; index++) {
        bytes[index] = 0;
    }
    hash_store(bytes, user_state_build_hash(build));
    bytes[MARK_VERSION_AT] = MARK_VERSION;
    bytes[MARK_REQUEST_AT] = request;
    for (int at = 0; at < USER_STATE_BUILD_BYTES && build[at] != '\0'; at++) {
        bytes[MARK_BUILD_AT + at] = (uint8_t)build[at];
    }
}

static void mark_build(const uint8_t *bytes, char *build)
{
    for (int at = 0; at < USER_STATE_BUILD_BYTES; at++) {
        build[at] = (char)bytes[MARK_BUILD_AT + at];
    }
    build[USER_STATE_BUILD_BYTES] = '\0';
}

static bool mark_valid(const uint8_t *bytes)
{
    char build[USER_STATE_BUILD_BYTES + 1];
    mark_build(bytes, build);
    return bytes[MARK_VERSION_AT] == MARK_VERSION &&
           hash_load(bytes) == user_state_build_hash(build);
}

uint8_t user_state_mark_request(const uint8_t *bytes)
{
    bool valid = mark_valid(bytes) && bytes[MARK_REQUEST_AT] <= USER_STATE_BACKUPS;
    return valid ? bytes[MARK_REQUEST_AT] : 0;
}

static bool read_mark(const UserStateIo *io, uint8_t *mark)
{
    static const UserStatePlace PLACE = {USER_STATE_INTERNAL, USER_STATE_MARK_PATH};
    return io->size(io->context, &PLACE) == USER_STATE_MARK_BYTES &&
           io->read(io->context, &PLACE, (UserStateChunk){0, mark, USER_STATE_MARK_BYTES});
}

static bool write_mark(const UserStateIo *io, const char *build, bool present)
{
    static const UserStatePlace PLACE = {USER_STATE_INTERNAL, USER_STATE_MARK_PATH};
    uint8_t mark[USER_STATE_MARK_BYTES];
    user_state_mark(build, 0, mark);
    bool sized = present || io->create(io->context, &PLACE, USER_STATE_MARK_BYTES);
    return sized && io->write(io->context, &PLACE, (UserStateChunk){0, mark, sizeof mark});
}

bool user_state_run(const UserStateIo *io, const char *build, bool changed)
{
    uint8_t mark[USER_STATE_MARK_BYTES] = {0};
    bool present = read_mark(io, mark);
    bool known = present && mark_valid(mark);
    uint8_t request = known ? user_state_mark_request(mark) : 0;
    char previous[USER_STATE_BUILD_BYTES + 1];
    mark_build(mark, previous);
    if (request == 0 && (!changed || (known && same_build(previous, build)))) {
        return true;
    }
    if (request == 0 && known && previous[0] != '\0' && !user_state_backup(io, previous)) {
        return false;
    }
    bool restored = request == 0 || user_state_restore(io, (uint8_t)(request - 1));
    if (known) {
        user_state_settle(io);
    }
    bool marked = write_mark(io, build, present);
    return restored && marked;
}
