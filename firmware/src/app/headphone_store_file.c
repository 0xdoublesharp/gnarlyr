#include "headphone_store.h"

#include <stddef.h>

#include "hal/storage.h"
#include "path_file.h"

bool headphone_store_internal_read(const char *path, uint8_t *scratch, uint32_t capacity,
                                   const PathSlice *slice)
{
    if (slice->offset > capacity || slice->length > capacity - slice->offset ||
        !hal_internal_file_read(path, scratch, slice->offset + slice->length)) {
        return false;
    }
    uint8_t *destination = slice->bytes;
    for (uint32_t index = 0; index < slice->length; index++) {
        destination[index] = scratch[slice->offset + index];
    }
    return true;
}

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t prefix[HEADPHONE_FILE_BYTES];
    PathSlice slice = {offset, bytes, length};
    return headphone_store_internal_read(HEADPHONE_STORE_PATH, prefix, sizeof prefix, &slice);
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
