#include "core/parametric.h"

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
    for (int index = 0; index < eq->count && index < PARAMETRIC_FILTERS; index++) {
        if (eq->filters[index].gain != 0 &&
            parametric_design(&sections[count], &eq->filters[index], rate)) {
            count++;
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
    int allowed = -(peak > margin ? peak : margin);
    allowed = eq->preamp < allowed ? eq->preamp : allowed;
    uint64_t grid = (uint64_t)parametric_amplitude(allowed) * 32768 >> PARAMETRIC_BITS;
    uint16_t safe = parametric_cascade_pre_gain(sections, count);
    return (uint16_t)(safe < grid ? safe : grid);
}
