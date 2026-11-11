#include "settings/headphone_file.h"

#include <stddef.h>

static const StoreKind KIND = {.magic = {'D', 'S', 'H', 'F'}, .version = 1, .inline_payload = true};

static StoreSlot slot_of(const HeadphoneFile *owner, uint8_t bank)
{
    return (StoreSlot){
        .file = &owner->file, .kind = &KIND, .offset = (uint32_t)bank * STORE_BLOCK_BYTES};
}

StoreResult headphone_file_save(HeadphoneFile *owner, const HeadphoneProfiles *profiles)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    if (owner->needs_reload || owner->file.size < HEADPHONE_FILE_BYTES ||
        owner->file.source.read == NULL || owner->file.write == NULL ||
        !headphone_profiles_encode(profiles, block + STORE_FIELD_BYTES,
                                   HEADPHONE_PROFILES_FILE_SIZE)) {
        return STORE_FAILED;
    }
    uint32_t generation = owner->valid ? owner->generation + 1 : 1;
    uint8_t bank = owner->valid ? (uint8_t)(1 - owner->bank) : 0;
    StoreHeader header = {.generation = generation, .bytes = HEADPHONE_PROFILES_FILE_SIZE};
    store_encode(&KIND, &header, block);
    StoreSlot slot = slot_of(owner, bank);
    StoreResult result = store_publish(&slot, block, owner->valid ? &owner->generation : NULL);
    owner->needs_reload = result == STORE_UNCERTAIN;
    if (result == STORE_DONE) {
        owner->generation = generation;
        owner->bank = bank;
        owner->valid = true;
    }
    return result;
}
