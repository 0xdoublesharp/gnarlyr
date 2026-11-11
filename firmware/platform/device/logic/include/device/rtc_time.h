#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/rtc_registers.h"

bool rtc_time_registers(uint32_t utc_s, uint8_t registers[RTC_TIME_REGISTERS]);
bool rtc_time_utc(const uint8_t registers[RTC_TIME_REGISTERS], uint32_t *utc_s);
bool rtc_time_trusted(const uint8_t registers[RTC_TIME_REGISTERS], uint32_t *utc_s);
