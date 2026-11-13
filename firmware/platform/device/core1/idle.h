#pragma once

#include <stdint.h>

#include "device/core1_stream.h"

void core1_idle_start(void);
void core1_idle(const Core1Stream *stream);
void core1_request_clock(uint32_t mhz);
