#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    PARAMETRIC_FILTERS = 10,
    PARAMETRIC_BITS = 28,
    PARAMETRIC_MIN_HZ = 20,
    PARAMETRIC_MAX_HZ = 20000,
    PARAMETRIC_MIN_Q = 30,
    PARAMETRIC_MAX_Q = 1000,
    PARAMETRIC_MAX_GAIN = 120,
    PARAMETRIC_MIN_PREAMP = -600,
    PARAMETRIC_MAX_PREAMP = 120
};

typedef enum ParametricType {
    PARAMETRIC_PEAK,
    PARAMETRIC_LOW_SHELF,
    PARAMETRIC_HIGH_SHELF
} ParametricType;

typedef struct ParametricFilter {
    uint16_t hz;
    uint16_t q;
    int16_t gain;
    uint8_t type;
    bool enabled;
} ParametricFilter;

typedef struct ParametricEq {
    ParametricFilter filters[PARAMETRIC_FILTERS];
    int16_t preamp;
    uint8_t count;
} ParametricEq;

typedef struct ParametricSection {
    int32_t b0;
    int32_t b1;
    int32_t b2;
    int32_t a1;
    int32_t a2;
} ParametricSection;

bool parametric_filter_valid(const ParametricFilter *filter);
bool parametric_valid(const ParametricEq *eq);
bool parametric_same(const ParametricEq *first, const ParametricEq *second);
bool parametric_design(ParametricSection *section, const ParametricFilter *filter, uint32_t rate);
int64_t parametric_cosine(int64_t angle);
uint32_t parametric_amplitude(int tenths);
int16_t parametric_decibels(uint64_t power);
uint64_t parametric_power(const ParametricSection *section, uint32_t hz, uint32_t rate);

uint64_t parametric_bound(const ParametricSection *section);
uint16_t parametric_cascade_pre_gain(const ParametricSection *sections, int count);
int16_t parametric_response(const ParametricEq *eq, uint32_t hz, uint32_t rate);
uint16_t parametric_pre_gain(const ParametricEq *eq, uint32_t rate);

uint64_t parametric_power_at(const ParametricSection *section, int64_t cosine, int64_t sine);
int parametric_cascade_response(const ParametricSection *sections, int count, uint32_t hz,
                                uint32_t rate);
