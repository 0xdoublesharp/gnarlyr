#pragma once

#include "playback/store.h"
#include "settings/headphone_profile.h"

enum {
    HEADPHONE_FILE_BYTES = 2 * STORE_BLOCK_BYTES
};

typedef struct HeadphoneFile {
    StoreFile file;
    uint32_t generation;
    uint8_t bank;
    bool valid;
    bool needs_reload;
} HeadphoneFile;

StoreLoad headphone_file_open(HeadphoneFile *owner, StoreFile file, HeadphoneProfiles *profiles);
StoreResult headphone_file_save(HeadphoneFile *owner, const HeadphoneProfiles *profiles);
