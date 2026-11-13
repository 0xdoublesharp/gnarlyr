#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/format_chain.h"

#define USER_STATE_FOLDER "/" FIRMWARE_DATA_DIRECTORY "/"
#define USER_STATE_MARK_PATH USER_STATE_FOLDER "continuity.bin"
#define USER_STATE_LIST_PATH USER_STATE_FOLDER "backup/list.bin"

enum {
    USER_STATE_NAME_CAPACITY = 20,
    USER_STATE_PATH_CAPACITY = 48,
    USER_STATE_BUILD_BYTES = 16,
    USER_STATE_BACKUPS = 2,
    USER_STATE_LIST_BYTES = 8 + USER_STATE_BACKUPS * USER_STATE_BUILD_BYTES + 4,
    USER_STATE_MARK_BYTES = 8 + USER_STATE_BUILD_BYTES,
    USER_STATE_HEAD_BYTES = 8,
    USER_STATE_FILE_LIMIT = 2 * 1024 * 1024,
    USER_STATE_TAPES = 32,
    USER_STATE_MARK_VERSION_AT = 4,
    USER_STATE_MARK_REQUEST_AT = 5,
    USER_STATE_NO_SLOT = 0xFF
};

#define USER_STATE_MISSING UINT32_MAX

typedef enum UserStateVolume {
    USER_STATE_INTERNAL,
    USER_STATE_MUSIC
} UserStateVolume;

typedef struct UserStateKind {
    const char *name;
    uint8_t volume;
    uint8_t magic[4];
    uint8_t magic_length;
    uint8_t version_at;
    uint8_t version_width;
    uint16_t current;
} UserStateKind;

typedef struct UserStatePlace {
    uint8_t volume;
    const char *path;
} UserStatePlace;

typedef struct UserStateChunk {
    uint32_t offset;
    uint8_t *bytes;
    uint32_t length;
} UserStateChunk;

typedef struct UserStateIo {
    uint32_t (*size)(void *context, const UserStatePlace *place);
    bool (*read)(void *context, const UserStatePlace *place, UserStateChunk chunk);
    bool (*create)(void *context, const UserStatePlace *place, uint32_t size);
    bool (*write)(void *context, const UserStatePlace *place, UserStateChunk chunk);
    bool (*remove)(void *context, const UserStatePlace *place);
    void *context;
    uint8_t *buffer;
    uint32_t buffer_bytes;
} UserStateIo;

typedef struct UserStateList {
    char builds[USER_STATE_BACKUPS][USER_STATE_BUILD_BYTES + 1];
    uint8_t newest;
} UserStateList;

uint32_t user_state_count(void);
UserStateKind user_state_kind(uint32_t index, char *name);
FormatAge user_state_age(const UserStateKind *kind, const uint8_t *head, uint32_t length);
bool user_state_path(char *path, const char *folder, const char *name);
bool user_state_copy(const UserStateIo *io, const UserStatePlace *from, const UserStatePlace *to);

void user_state_list_encode(const UserStateList *list, uint8_t *bytes);
UserStateList user_state_list_decode(const uint8_t *bytes);
int user_state_list_order(const UserStateList *list, int position);
bool user_state_backup(const UserStateIo *io, const char *build);
bool user_state_restore(const UserStateIo *io, uint8_t slot);
void user_state_settle(const UserStateIo *io);

uint32_t user_state_build_hash(const char *build);
void user_state_mark(const char *build, uint8_t request, uint8_t *bytes);
uint8_t user_state_mark_request(const uint8_t *bytes);
bool user_state_run(const UserStateIo *io, const char *build, bool changed);
