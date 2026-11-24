#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/store.h"

typedef struct PathFile {
    const char *path;
    uint32_t size;
} PathFile;

typedef struct PathSlice {
    uint32_t offset;
    void *bytes;
    uint32_t length;
} PathSlice;

bool path_file_read(void *context, uint32_t offset, void *bytes, uint32_t length);
bool path_file_write(void *context, uint32_t offset, const void *bytes, uint32_t length);
StoreFile path_file_store(PathFile *file);
bool path_file_find(PathFile *file);
bool path_file_prepare(PathFile *file, uint32_t size);
