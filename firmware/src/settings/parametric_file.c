#include "settings/parametric_file.h"

#include <stddef.h>

#include "core/hash.h"

static const StoreKind KIND = {.magic = {'P', 'E', 'Q', 'F'}, .version = 1};

static bool zero(const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool encode_record(const ParametricEq *eq, uint8_t *bytes)
{
    if (!parametric_valid(eq)) {
        return false;
    }
    bytes[0] = eq->count;
    store_put16(bytes + 2, (uint16_t)eq->preamp);
    for (int index = 0; index < eq->count; index++) {
        const ParametricFilter *filter = &eq->filters[index];
        uint8_t *record = bytes + 4 + 8 * index;
        store_put16(record, filter->hz);
        store_put16(record + 2, filter->q);
        store_put16(record + 4, (uint16_t)filter->gain);
        record[6] = filter->type;
        record[7] = filter->enabled ? 1 : 0;
    }
    return true;
}

bool parametric_bank_encode(const ParametricBank *bank, uint8_t *bytes)
{
    for (int index = 0; index < PARAMETRIC_PAYLOAD_BYTES; index++) {
        bytes[index] = 0;
    }
    bytes[0] = bank->used;
    if (!encode_record(&bank->custom, bytes + 4)) {
        return false;
    }
    for (int index = 0; index < PARAMETRIC_SLOT_COUNT; index++) {
        if ((bank->used >> index & 1) != 0 &&
            !encode_record(&bank->slots[index],
                           bytes + 4 + (index + 1) * PARAMETRIC_RECORD_BYTES)) {
            return false;
        }
    }
    return true;
}

static bool decode_record(const uint8_t *bytes, ParametricEq *eq)
{
    *eq = (ParametricEq){.count = bytes[0], .preamp = (int16_t)store_get16(bytes + 2)};
    if (bytes[1] != 0 || eq->count > PARAMETRIC_FILTERS) {
        return false;
    }
    for (int index = 0; index < PARAMETRIC_FILTERS; index++) {
        const uint8_t *record = bytes + 4 + 8 * index;
        if (index >= eq->count) {
            if (!zero(record, 8)) {
                return false;
            }
        } else {
            eq->filters[index] = (ParametricFilter){.hz = store_get16(record),
                                                    .q = store_get16(record + 2),
                                                    .gain = (int16_t)store_get16(record + 4),
                                                    .type = record[6],
                                                    .enabled = record[7] == 1};
            if (record[7] > 1) {
                return false;
            }
        }
    }
    return parametric_valid(eq);
}

bool parametric_bank_decode(const uint8_t *bytes, ParametricBank *bank)
{
    ParametricBank decoded = {.used = bytes[0]};
    if (!zero(bytes + 1, 3) || !decode_record(bytes + 4, &decoded.custom)) {
        return false;
    }
    for (int index = 0; index < PARAMETRIC_SLOT_COUNT; index++) {
        const uint8_t *record = bytes + 4 + (index + 1) * PARAMETRIC_RECORD_BYTES;
        if ((decoded.used >> index & 1) != 0 ? !decode_record(record, &decoded.slots[index])
                                             : !zero(record, PARAMETRIC_RECORD_BYTES)) {
            return false;
        }
    }
    *bank = decoded;
    return true;
}

typedef struct Reader {
    ParametricFile *owner;
    ParametricBank *bank;
} Reader;

static StoreLoad check_record(void *context, uint8_t bank, const StoreHeader *header)
{
    Reader *reader = context;
    uint8_t bytes[PARAMETRIC_PAYLOAD_BYTES];
    if (header->bytes != sizeof bytes || header->binding != 0 || header->count != 0) {
        return STORE_CORRUPT;
    }
    uint32_t offset = (uint32_t)bank * PARAMETRIC_BANK_BYTES + STORE_BLOCK_BYTES;
    for (uint32_t at = 0; at < sizeof bytes; at += STORE_BLOCK_BYTES) {
        uint32_t length =
            sizeof bytes - at < STORE_BLOCK_BYTES ? sizeof bytes - at : STORE_BLOCK_BYTES;
        if (!store_file_read(&reader->owner->file, offset + at, bytes + at, length)) {
            return STORE_UNREADABLE;
        }
    }
    return hash_update(HASH_START, bytes, sizeof bytes) == header->hash &&
                   parametric_bank_decode(bytes, reader->bank)
               ? STORE_READY
               : STORE_CORRUPT;
}

StoreLoad parametric_file_open(ParametricFile *owner, StoreFile file, ParametricBank *bank)
{
    *owner = (ParametricFile){.file = file};
    if (file.size < PARAMETRIC_FILE_BYTES || file.source.read == NULL || bank == NULL) {
        owner->needs_reload = true;
        return STORE_UNREADABLE;
    }
    Reader reader = {.owner = owner, .bank = bank};
    StorePair pair = {.file = &owner->file,
                      .kind = &KIND,
                      .bank_bytes = PARAMETRIC_BANK_BYTES,
                      .check = check_record,
                      .context = &reader};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    owner->needs_reload = store_blocking(load);
    if (load == STORE_READY) {
        owner->generation = choice.header.generation;
        owner->bank = choice.bank;
        owner->valid = true;
    }
    return load;
}
