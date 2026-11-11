#include "parametric_store.h"

#include <stddef.h>

#include "hal/storage.h"

static ParametricBank bank;

ParametricBank *parametric_store_bank(void)
{
    return &bank;
}

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t prefix[PARAMETRIC_FILE_BYTES];
    if (offset > sizeof prefix || length > sizeof prefix - offset ||
        !hal_internal_file_read(PARAMETRIC_STORE_PATH, prefix, offset + length)) {
        return false;
    }
    uint8_t *destination = bytes;
    for (uint32_t index = 0; index < length; index++) {
        destination[index] = prefix[offset + index];
    }
    return true;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    return hal_internal_file_write_at(PARAMETRIC_STORE_PATH, offset, bytes, length);
}

void parametric_store_read(ParametricStore *store, ParametricEq *custom)
{
    bool uncertain = store->uncertain;
    *store = (ParametricStore){.uncertain = uncertain};
    StoreFile file = {
        .source = {read_file, NULL}, .write = write_file, .size = PARAMETRIC_FILE_BYTES};
    bank = (ParametricBank){0};
    parametric_file_open(&store->file, file, &bank);
    store->file.needs_reload |= uncertain;
    if (custom != NULL) {
        *custom = bank.custom;
    }
}
