#pragma once

#include "playback/mix_identity.h"

enum {
    PERSONAL_TAG_LIMIT = 32,
    PERSONAL_TAG_NAME_BYTES = 33,
    PERSONAL_TAG_TRACK_LIMIT = UINT16_MAX,
    PERSONAL_TAG_ROW_BYTES = 8 + MIX_RECORD_MAX,
    PERSONAL_TAG_CATALOG_BYTES = 4 + PERSONAL_TAG_LIMIT * PERSONAL_TAG_NAME_BYTES
};

typedef struct PersonalTagFile {
    const char *path;
    uint32_t size;
} PersonalTagFile;

typedef struct PersonalTagStorage {
    StoreFile files[2];
    bool (*prepare)(void *context, uint8_t bank, uint32_t size, StoreFile *file);
    void *context;
} PersonalTagStorage;

typedef struct PersonalTagCatalog {
    uint32_t active;
    char names[PERSONAL_TAG_LIMIT][PERSONAL_TAG_NAME_BYTES];
} PersonalTagCatalog;

typedef enum PersonalTagOperation {
    PERSONAL_TAG_CREATE,
    PERSONAL_TAG_RENAME,
    PERSONAL_TAG_DELETE,
    PERSONAL_TAG_ASSIGN
} PersonalTagOperation;

typedef struct PersonalTagChange {
    PersonalTagOperation operation;
    const char *name;
    uint32_t mask;
    uint16_t track;
    uint8_t tag;
} PersonalTagChange;

typedef struct PersonalTags {
    const Library *library;
    PersonalTagFile files[2];
    PersonalTagStorage storage;
    StoreHeader headers[2];
    StoreLoad banks[2];
    PersonalTagCatalog catalog;
    PersonalTagCatalog pending;
    StoreWrite writer;
    StoreScan scan;
    MixIdentity identity;
    MixIdentity previous;
    uint32_t mask;
    uint32_t position;
    uint32_t target;
    uint32_t source_row;
    uint32_t output_row;
    uint32_t rows;
    uint8_t active_bank;
    uint8_t bank;
    uint8_t stage;
    bool has_active;
    bool inserted;
    bool assigning;
    bool target_found;
    bool fallback;
    StoreLoad load;
    StoreResult result;
} PersonalTags;

extern const char PERSONAL_TAG_PATH_A[];
extern const char PERSONAL_TAG_PATH_B[];

StoreLoad personal_tags_init(PersonalTags *tags, const Library *library);
StoreLoad personal_tags_open(PersonalTags *tags, const Library *library,
                             PersonalTagStorage storage);
void personal_tags_attach(PersonalTags *tags, const Library *library);
StoreResult personal_tags_step(PersonalTags *tags);
void personal_tags_cancel(PersonalTags *tags);
const char *personal_tags_name(const PersonalTags *tags, uint8_t tag);
bool personal_tags_change(PersonalTags *tags, PersonalTagChange change);
bool personal_tags_mask(const PersonalTags *tags, uint16_t track, uint32_t *mask);
bool personal_tags_matches(const PersonalTags *tags, uint16_t track, uint32_t wanted,
                           bool *matches);
uint8_t personal_tags_progress(const PersonalTags *tags);
