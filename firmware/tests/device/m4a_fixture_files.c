#include "m4a_fixture_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

M4aFile m4a_file_load(const char *relative_path)
{
    char path[512];
    M4aFile result = {NULL, 0};
    snprintf(path, sizeof path, "%s/%s", ECHO_FIXTURE_DIR, relative_path);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return result;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    result.bytes = malloc((size_t)size);
    if (result.bytes != NULL && fread(result.bytes, 1, (size_t)size, file) == (size_t)size) {
        result.size = (uint32_t)size;
    }
    fclose(file);
    return result;
}

void m4a_file_free(M4aFile *file)
{
    free(file->bytes);
    *file = (M4aFile){NULL, 0};
}

static bool read_file(void *context, uint32_t offset, void *destination, uint32_t length)
{
    const M4aFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(destination, file->bytes + offset, length);
    return true;
}

FlacInput m4a_file_input(const M4aFile *file)
{
    return (FlacInput){.read = read_file, .context = (void *)(uintptr_t)file, .size = file->size};
}
