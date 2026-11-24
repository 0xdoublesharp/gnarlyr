#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/updater.h"

enum {
    SLOT_COPY_BUFFER_WORDS = 2 * UPDATER_CHUNK_BYTES / 4
};

typedef struct SlotCopyJob {
    const UpdateStorage *storage;
    const UpdateFile *file;
    uint32_t from;
    uint32_t to;
    uint32_t *buffer;
    UpdateProgress progress;
    uint32_t chunks;
} SlotCopyJob;

typedef enum SlotValidity {
    SLOT_UNREADABLE,
    SLOT_INVALID,
    SLOT_VALID
} SlotValidity;

bool slot_copy_run(const SlotCopyJob *job);
SlotValidity slot_copy_check(const UpdateStorage *storage, uint32_t slot, uint32_t *buffer);
bool slot_copy_is_valid(const UpdateStorage *storage, uint32_t slot, uint32_t *buffer);
