#pragma once

#include <stdint.h>

#define EXTENSION_MODULE_NUMBER 8u

typedef struct ExtensionModuleHead {
    uint32_t magic;
    const void *debug;
} ExtensionModuleHead;
