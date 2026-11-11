#include "settings/headphone_file.h"

#include <stddef.h>

static const StoreKind KIND = {.magic = {'D', 'S', 'H', 'F'}, .version = 1, .inline_payload = true};

typedef struct Reader {
    HeadphoneFile *owner;
    HeadphoneProfiles profiles;
} Reader;

static StoreSlot slot_of(const HeadphoneFile *owner, uint8_t bank)
{
    return (StoreSlot){
        .file = &owner->file, .kind = &KIND, .offset = (uint32_t)bank * STORE_BLOCK_BYTES};
}

static StoreLoad check_record(void *context, uint8_t bank, const StoreHeader *header)
{
    Reader *reader = context;
    StoreSlot slot = slot_of(reader->owner, bank);
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader again;
    StoreLoad load = store_load(&slot, block, &again);
    if (load != STORE_READY) {
        return load;
    }
    bool valid =
        again.generation == header->generation && again.binding == 0 && again.count == 0 &&
        again.bytes == HEADPHONE_PROFILES_FILE_SIZE &&
        headphone_profiles_decode(block + STORE_FIELD_BYTES, again.bytes, &reader->profiles);
    return valid ? STORE_READY : STORE_CORRUPT;
}

StoreLoad headphone_file_open(HeadphoneFile *owner, StoreFile file, HeadphoneProfiles *profiles)
{
    *owner = (HeadphoneFile){.file = file};
    if (file.size < HEADPHONE_FILE_BYTES || file.source.read == NULL || profiles == NULL) {
        owner->needs_reload = true;
        return STORE_UNREADABLE;
    }
    Reader reader = {.owner = owner};
    StorePair pair = {.file = &owner->file,
                      .kind = &KIND,
                      .bank_bytes = STORE_BLOCK_BYTES,
                      .check = check_record,
                      .context = &reader};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    owner->needs_reload = store_blocking(load);
    if (load == STORE_READY) {
        owner->generation = choice.header.generation;
        owner->bank = choice.bank;
        owner->valid = true;
        *profiles = reader.profiles;
    }
    return load;
}
