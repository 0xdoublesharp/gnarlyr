#include "device/updater.h"

#include <stddef.h>

#include "core/seam.h"
#include "device/slot_copy.h"

enum {
    CRC_BYTES = 4,
    PASSES = 2
};

typedef struct PassProgress {
    UpdateProgress outer;
    uint32_t pass;
} PassProgress;

static void pass_report(void *context, uint32_t done, uint32_t total)
{
    const PassProgress *pass = context;
    if (pass->outer.report != NULL) {
        pass->outer.report(pass->outer.context, pass->pass * total + done, PASSES * total);
    }
}

static bool slot_1_first(const UpdateStorage *storage, uint32_t *buffer)
{
    return !slot_copy_is_valid(storage, storage->slot_1, buffer) &&
           slot_copy_is_valid(storage, storage->slot_2, buffer);
}

CORE_SEAM bool updater_install(const UpdateFile *file, const UpdateStorage *storage,
                               UpdateProgress progress, uint32_t *buffer)
{
    if (buffer == NULL) {
        return false;
    }
    PassProgress pass = {progress, 0};
    uint32_t chunks = (file->size - CRC_BYTES) / UPDATER_CHUNK_BYTES;
    bool first_is_1 = slot_1_first(storage, buffer);
    uint32_t first = first_is_1 ? storage->slot_1 : storage->slot_2;
    uint32_t second = first_is_1 ? storage->slot_2 : storage->slot_1;
    SlotCopyJob written = {storage, file, 0, first, buffer, {pass_report, &pass}, chunks};
    SlotCopyJob copied = {storage, NULL, first, second, buffer, {pass_report, &pass}, chunks};
    if (!slot_copy_run(&written)) {
        return false;
    }
    pass.pass = 1;
    return slot_copy_run(&copied);
}
