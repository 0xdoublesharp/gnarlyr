#include "settings/parametric_file.h"

#include <stddef.h>

static const StoreKind KIND = {.magic = {'P', 'E', 'Q', 'F'}, .version = 1};

static bool fill(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    const uint8_t *bytes = context;
    for (uint32_t index = 0; index < count; index++) {
        block->bytes[index] = bytes[position + index];
    }
    return true;
}

StoreResult parametric_file_save(ParametricFile *owner, const ParametricBank *bank)
{
    uint8_t bytes[PARAMETRIC_PAYLOAD_BYTES];
    if (owner->needs_reload || owner->file.size < PARAMETRIC_FILE_BYTES ||
        owner->file.source.read == NULL || owner->file.write == NULL ||
        !parametric_bank_encode(bank, bytes)) {
        return STORE_FAILED;
    }
    uint32_t generation = owner->valid ? owner->generation + 1 : 1;
    uint8_t next = owner->valid ? (uint8_t)(1 - owner->bank) : 0;
    StoreSlot slot = {.file = &owner->file, .kind = &KIND, .offset = next * PARAMETRIC_BANK_BYTES};
    StoreWrite job;
    store_write_begin(&job, owner->valid ? &owner->generation : NULL);
    job.header = (StoreHeader){.generation = generation, .bytes = sizeof bytes};
    StoreFill source = {.fill = fill, .context = bytes};
    while (store_write_step(&job, &slot, source) == STORE_WORKING) {
    }
    owner->needs_reload = job.result == STORE_UNCERTAIN;
    if (job.result == STORE_DONE) {
        owner->generation = generation;
        owner->bank = next;
        owner->valid = true;
    }
    return job.result;
}
