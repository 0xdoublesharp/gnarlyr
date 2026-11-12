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

CORE_SEAM bool updater_install(const UpdateFile *file, const UpdateStorage *storage,
                               UpdateProgress progress, uint32_t *buffer)
{
    if (buffer == NULL) {
        return false;
    }
    PassProgress pass = {progress, 0};
    uint32_t chunks = (file->size - CRC_BYTES) / UPDATER_CHUNK_BYTES;
    SlotCopyJob second = {storage, file, 0, storage->slot_2, buffer, {pass_report, &pass}, chunks};
    SlotCopyJob first = {
        storage, NULL, storage->slot_2, storage->slot_1, buffer, {pass_report, &pass}, chunks};
    if (!slot_copy_run(&second)) {
        return false;
    }
    pass.pass = 1;
    return slot_copy_run(&first);
}
