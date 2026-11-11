#include "settings/sound_shaping.h"

#include "core/string_builder.h"

static const ParametricFilter FILTERS[SOUND_SHAPING_COUNT] = {
    {300, 70, 10, PARAMETRIC_PEAK, true},     {6000, 71, 10, PARAMETRIC_HIGH_SHELF, true},
    {80, 71, 10, PARAMETRIC_LOW_SHELF, true}, {90, 140, 10, PARAMETRIC_PEAK, true},
    {2000, 70, 10, PARAMETRIC_PEAK, true},    {12000, 71, 10, PARAMETRIC_HIGH_SHELF, true},
    {350, 90, -10, PARAMETRIC_PEAK, true},
};

static const StringId LABELS[SOUND_SHAPING_COUNT] = {
    STR_SHAPE_WARMTH, STR_SHAPE_BRIGHTNESS, STR_SHAPE_BASS,   STR_SHAPE_PUNCH,
    STR_SHAPE_VOCALS, STR_SHAPE_AIR,        STR_SHAPE_CLARITY};

static const StringId ENDS[SOUND_SHAPING_COUNT][2] = {
    {STR_SHAPE_COOL, STR_SHAPE_WARM},     {STR_SHAPE_DARK, STR_SHAPE_BRIGHT},
    {STR_SHAPE_LIGHT, STR_SHAPE_DEEP},    {STR_SHAPE_SOFT, STR_SHAPE_PUNCHY},
    {STR_SHAPE_DISTANT, STR_SHAPE_CLOSE}, {STR_SHAPE_CLOSED, STR_SHAPE_AIRY},
    {STR_SHAPE_MUDDY, STR_SHAPE_CLEAN}};

StringId sound_shaping_label(int slider)
{
    return LABELS[slider];
}

StringId sound_shaping_end(int slider, bool positive)
{
    return ENDS[slider][positive ? 1 : 0];
}

bool sound_shaping_build(ParametricEq *result, const ParametricEq *base, const SoundShaping *shape)
{
    if (!shape->active || !parametric_valid(base) || shape->base_count > base->count) {
        return false;
    }
    ParametricEq next = *base;
    next.count = shape->base_count;
    next.preamp = base->preamp < 0 ? base->preamp : 0;
    for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
        int step = shape->positions[slider];
        if (step < -SOUND_SHAPING_LIMIT || step > SOUND_SHAPING_LIMIT ||
            (step != 0 && next.count == PARAMETRIC_FILTERS)) {
            return false;
        }
        if (step != 0) {
            ParametricFilter filter = FILTERS[slider];
            filter.gain = (int16_t)(filter.gain * step);
            next.filters[next.count++] = filter;
        }
    }
    *result = next;
    return true;
}

bool sound_shaping_valid(const SoundShaping *shape, const ParametricEq *eq)
{
    if (!shape->active) {
        if (shape->base_count != 0) {
            return false;
        }
        for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
            if (shape->positions[slider] != 0) {
                return false;
            }
        }
        return true;
    }
    ParametricEq expected;
    return sound_shaping_build(&expected, eq, shape) && parametric_same(&expected, eq);
}

void sound_shaping_words(const SoundShaping *shape, char *text, uint32_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    int words = 0;
    for (int slider = 0; slider < SOUND_SHAPING_COUNT && words < 2; slider++) {
        if (shape->positions[slider] == 0) {
            continue;
        }
        if (words++ != 0) {
            string_builder_append_char(&builder, ' ');
            string_builder_append(&builder, i18n_text(STR_SHAPE_SEPARATOR));
            string_builder_append_char(&builder, ' ');
        }
        string_builder_append(&builder,
                              i18n_text(sound_shaping_end(slider, shape->positions[slider] > 0)));
    }
    if (words == 0) {
        string_builder_append(&builder, i18n_text(STR_SHAPE_NEUTRAL));
    }
}
