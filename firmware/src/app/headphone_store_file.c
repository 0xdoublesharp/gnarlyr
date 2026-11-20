#include "headphone_store.h"

#include <stddef.h>

#include "hal/storage.h"

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t prefix[HEADPHONE_FILE_BYTES];
    if (offset > sizeof prefix || length > sizeof prefix - offset ||
        !hal_internal_file_read(HEADPHONE_STORE_PATH, prefix, offset + length)) {
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
    return hal_internal_file_write_at(HEADPHONE_STORE_PATH, offset, bytes, length);
}

StoreFile headphone_store_file(void)
{
    return (StoreFile){
        .source = {read_file, NULL}, .write = write_file, .size = HEADPHONE_FILE_BYTES};
}
