#include "path_file.h"

#include "hal/storage.h"

bool path_file_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const PathFile *source = context;
    HalFile file;
    if (!hal_file_open(&file, source->path)) {
        return false;
    }
    bool read = offset <= file.size && length <= file.size - offset &&
                hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return read;
}

bool path_file_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const PathFile *file = context;
    return hal_file_write_at(file->path, offset, bytes, length);
}

StoreFile path_file_store(PathFile *file)
{
    return (StoreFile){
        .source = {path_file_read, file}, .write = path_file_write, .size = file->size};
}

bool path_file_find(PathFile *file)
{
    HalFile handle;
    file->size = 0;
    if (!hal_file_open(&handle, file->path)) {
        return false;
    }
    file->size = handle.size;
    hal_file_close(&handle);
    return true;
}

bool path_file_prepare(PathFile *file, uint32_t size)
{
    if (!path_file_find(file)) {
        HalReserveResult result = hal_file_reserve(file->path, size);
        if (result != HAL_RESERVE_CREATED && result != HAL_RESERVE_EXISTS) {
            return false;
        }
        path_file_find(file);
    }
    return file->size >= size;
}
