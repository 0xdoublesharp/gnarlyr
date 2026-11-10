#include <stdio.h>
#include <string.h>

#include "core/i18n.h"
#include "hal/language.h"

enum {
    PATH_CAPACITY = 1024
};

static const char PACK_PATHS[] = ECHO_LANGUAGE_PACK_PATHS;
static uint8_t memory[I18N_PACK_CAPACITY];

static bool path_of(uint32_t index, char *path)
{
    const char *cursor = PACK_PATHS;
    for (uint32_t current = 0; *cursor != '\0'; current++) {
        size_t length = strcspn(cursor, "|");
        if (current == index && length < PATH_CAPACITY) {
            memcpy(path, cursor, length);
            path[length] = '\0';
            return true;
        }
        cursor += length + (cursor[length] == '|' ? 1u : 0u);
    }
    return false;
}

uint8_t *hal_language_memory(uint32_t *capacity)
{
    *capacity = sizeof memory;
    return memory;
}

bool hal_language_read(HalLanguagePack pack, uint32_t offset, void *buffer, uint32_t length)
{
    char path[PATH_CAPACITY];
    if (pack.copy != 0 || !path_of(pack.index, path)) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    bool read =
        fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, length, file) == length;
    fclose(file);
    return read;
}
