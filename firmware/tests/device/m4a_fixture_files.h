#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/flac.h"

typedef struct M4aFile {
    uint8_t *bytes;
    uint32_t size;
} M4aFile;

M4aFile m4a_file_load(const char *relative_path);
void m4a_file_free(M4aFile *file);
FlacInput m4a_file_input(const M4aFile *file);
