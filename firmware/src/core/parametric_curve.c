#include "core/parametric.h"

static const int64_t UNIT = INT64_C(1) << PARAMETRIC_BITS;

static uint64_t absolute(int64_t value)
{
    return (uint64_t)(value < 0 ? -value : value);
}

static uint64_t root(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint64_t tail_bound(const ParametricSection *k, int64_t h2, int64_t h3)
{
    int64_t discriminant = (int64_t)k->a1 * k->a1 + 4 * (int64_t)k->a2 * UNIT;
    if (discriminant > 0) {
        int64_t difference = (int64_t)root((uint64_t)discriminant);
        int64_t first = (k->a1 + difference) / 2;
        int64_t second = (k->a1 - difference) / 2;
        int64_t c1 = (h3 * UNIT - second * h2) / difference;
        int64_t c2 = h2 - c1;
        if (absolute(first) >= (uint64_t)UNIT || absolute(second) >= (uint64_t)UNIT) {
            return UINT32_MAX;
        }
        return absolute(c1) * (uint64_t)UNIT / ((uint64_t)UNIT - absolute(first)) +
               absolute(c2) * (uint64_t)UNIT / ((uint64_t)UNIT - absolute(second));
    }
    uint64_t radius = root((uint64_t)(-(int64_t)k->a2 * UNIT));
    uint64_t imaginary = root((uint64_t)(-discriminant)) / 2;
    if (imaginary == 0 || radius >= (uint64_t)UNIT) {
        return UINT32_MAX;
    }
    uint64_t envelope = absolute(h2) + absolute(h3 * UNIT - (int64_t)k->a1 * h2 / 2) / imaginary;
    return envelope * (uint64_t)UNIT / ((uint64_t)UNIT - radius);
}

uint64_t parametric_bound(const ParametricSection *k)
{
    int64_t h0 = k->b0;
    int64_t h1 = k->b1 + (int64_t)k->a1 * h0 / UNIT;
    int64_t h2 = k->b2 + ((int64_t)k->a1 * h1 + (int64_t)k->a2 * h0) / UNIT;
    int64_t h3 = ((int64_t)k->a1 * h2 + (int64_t)k->a2 * h1) / UNIT;
    uint64_t bound = absolute(h0) + absolute(h1) + tail_bound(k, h2, h3);
    return bound + bound / 256 + 4096;
}

int parametric_cascade_response(const ParametricSection *sections, int count, uint32_t hz,
                                uint32_t rate)
{
    int db = 0;
    int64_t angle = INT64_C(1686629714) * hz / rate;
    int64_t cosine = parametric_cosine(angle);
    int64_t sine = parametric_cosine(INT64_C(421657428) - angle);
    for (int index = 0; index < count; index++) {
        db += parametric_decibels(parametric_power_at(&sections[index], cosine, sine));
    }
    return db;
}

int16_t parametric_response(const ParametricEq *eq, uint32_t hz, uint32_t rate)
{
    int db = 0;
    if (!parametric_valid(eq) || rate == 0) {
        return 0;
    }
    for (int index = 0; index < eq->count && index < PARAMETRIC_FILTERS; index++) {
        ParametricSection section;
        if (parametric_design(&section, &eq->filters[index], rate)) {
            db += parametric_decibels(parametric_power(&section, hz, rate));
        }
    }
    return (int16_t)db;
}

uint16_t parametric_pre_gain(const ParametricEq *eq, uint32_t rate)
{
    if (!parametric_valid(eq) || rate == 0 || rate > 96000) {
        return 0;
    }
    ParametricSection sections[PARAMETRIC_FILTERS];
    int count = 0;
    uint64_t safe = 32768;
    for (int index = 0; index < eq->count && index < PARAMETRIC_FILTERS; index++) {
        if (eq->filters[index].gain != 0 &&
            parametric_design(&sections[count], &eq->filters[index], rate)) {
            uint64_t bound = parametric_bound(&sections[count++]);
            safe = safe * (uint64_t)UNIT / (bound > (uint64_t)UNIT ? bound : (uint64_t)UNIT);
        }
    }
    int peak = 0;
    int trough = 0;
    uint64_t frequency = UINT64_C(20) << 16;
    for (int point = 0; point < 512 + eq->count + 2; point++) {
        uint32_t hz = point < 512                ? (uint32_t)(frequency >> 16)
                      : point < 512 + eq->count  ? eq->filters[point - 512].hz
                      : point == 512 + eq->count ? 0
                                                 : rate / 2;
        hz = hz > rate / 2 ? rate / 2 : hz;
        int db = parametric_cascade_response(sections, count, hz, rate);
        peak = db > peak ? db : peak;
        trough = db < trough ? db : trough;
        frequency = frequency * 66429 >> 16;
    }
    int margin = count == 0 ? 0 : 20 - trough * 35 / 100;
    int allowed = -peak - margin;
    allowed = eq->preamp < allowed ? eq->preamp : allowed;
    uint64_t grid = (uint64_t)parametric_amplitude(allowed) * 32768 >> PARAMETRIC_BITS;
    safe = safe * parametric_amplitude(-20) >> PARAMETRIC_BITS;
    if (count == 0) {
        safe = 32768;
    }
    return (uint16_t)(safe < grid ? safe : grid);
}
