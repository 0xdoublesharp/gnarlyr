#pragma once

#include <stdbool.h>
#include <stdint.h>

bool store_file_read_open(void *context, uint32_t offset, void *bytes, uint32_t length);
