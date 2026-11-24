#include "store_file.h"

#include "hal/storage.h"

bool store_file_read_open(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const HalFile *file = context;
    return hal_file_read(*file, offset, bytes, length);
}
