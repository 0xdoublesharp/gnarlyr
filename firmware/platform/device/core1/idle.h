#pragma once

#include <stdint.h>

void core1_idle_start(void);
void core1_idle(void);
void core1_request_clock(uint32_t mhz);
