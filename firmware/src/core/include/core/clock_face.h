#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/string_builder.h"
#include "core/strings.h"

typedef enum ClockMeridiem {
    CLOCK_TWENTY_FOUR_HOUR,
    CLOCK_AM,
    CLOCK_PM
} ClockMeridiem;

typedef struct ClockFace {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    ClockMeridiem meridiem;
} ClockFace;

uint32_t clock_local_seconds(uint32_t utc_s, int16_t offset_minutes);
ClockFace clock_face(uint32_t local_s, bool twenty_four_hour);
void clock_face_digits(StringBuilder *builder, ClockFace face);
StringId clock_face_meridiem(ClockFace face);
void clock_face_text(StringBuilder *builder, ClockFace face);
