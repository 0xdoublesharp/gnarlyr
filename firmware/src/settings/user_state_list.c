#include "settings/user_state.h"

#include "core/hash.h"

enum {
    LIST_VERSION = 1,
    VERSION_AT = 4,
    LIST_NEWEST_AT = 5,
    LIST_BUILDS_AT = 8,
    LIST_HASH_AT = USER_STATE_LIST_BYTES - 4
};

static const uint8_t LIST_MAGIC[4] = {'D', 'S', 'B', 'U'};

static bool same_magic(const uint8_t *bytes)
{
    for (int index = 0; index < 4; index++) {
        if (bytes[index] != LIST_MAGIC[index]) {
            return false;
        }
    }
    return true;
}

void user_state_list_encode(const UserStateList *list, uint8_t *bytes)
{
    for (int index = 0; index < USER_STATE_LIST_BYTES; index++) {
        bytes[index] = index < 4 ? LIST_MAGIC[index] : 0;
    }
    bytes[VERSION_AT] = LIST_VERSION;
    bytes[LIST_NEWEST_AT] = list->newest;
    for (int slot = 0; slot < USER_STATE_BACKUPS; slot++) {
        uint8_t *build = bytes + LIST_BUILDS_AT + slot * USER_STATE_BUILD_BYTES;
        for (int at = 0; at < USER_STATE_BUILD_BYTES && list->builds[slot][at] != '\0'; at++) {
            build[at] = (uint8_t)list->builds[slot][at];
        }
    }
    hash_store(bytes + LIST_HASH_AT, hash_update(HASH_START, bytes, LIST_HASH_AT));
}

static bool list_valid(const uint8_t *bytes)
{
    return same_magic(bytes) && bytes[VERSION_AT] == LIST_VERSION &&
           hash_load(bytes + LIST_HASH_AT) == hash_update(HASH_START, bytes, LIST_HASH_AT);
}

UserStateList user_state_list_decode(const uint8_t *bytes)
{
    UserStateList list = {.newest = USER_STATE_NO_SLOT};
    if (!list_valid(bytes)) {
        return list;
    }
    for (int slot = 0; slot < USER_STATE_BACKUPS; slot++) {
        const uint8_t *build = bytes + LIST_BUILDS_AT + slot * USER_STATE_BUILD_BYTES;
        for (int at = 0; at < USER_STATE_BUILD_BYTES; at++) {
            list.builds[slot][at] = (char)build[at];
        }
    }
    uint8_t newest = bytes[LIST_NEWEST_AT];
    list.newest =
        newest < USER_STATE_BACKUPS && list.builds[newest][0] != '\0' ? newest : USER_STATE_NO_SLOT;
    return list;
}

int user_state_list_order(const UserStateList *list, int position)
{
    int newest = list->newest == USER_STATE_NO_SLOT ? 0 : list->newest;
    for (int step = 0; step < USER_STATE_BACKUPS; step++) {
        int slot = (newest + step) % USER_STATE_BACKUPS;
        if (list->builds[slot][0] != '\0' && position-- == 0) {
            return slot;
        }
    }
    return -1;
}
