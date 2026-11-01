#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    LOADER_REQUEST_LENGTH = 11
};

#define LOADER_REQUEST_PATH "/MASKROM.REQ"

typedef struct LoaderRequestFile {
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
    bool (*write)(void *context, uint32_t offset, const void *bytes, uint32_t length);
    uint32_t size;
    void *context;
} LoaderRequestFile;

bool loader_request_consume(const LoaderRequestFile *file);
