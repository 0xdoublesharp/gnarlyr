#include "core/parametric.h"

bool parametric_filter_valid(const ParametricFilter *filter)
{
    return filter->hz >= PARAMETRIC_MIN_HZ && filter->hz <= PARAMETRIC_MAX_HZ &&
           filter->q >= PARAMETRIC_MIN_Q && filter->q <= PARAMETRIC_MAX_Q &&
           filter->gain >= -PARAMETRIC_MAX_GAIN && filter->gain <= PARAMETRIC_MAX_GAIN &&
           filter->type <= PARAMETRIC_HIGH_SHELF;
}

bool parametric_valid(const ParametricEq *eq)
{
    if (eq->count > PARAMETRIC_FILTERS || eq->preamp < PARAMETRIC_MIN_PREAMP ||
        eq->preamp > PARAMETRIC_MAX_PREAMP) {
        return false;
    }
    for (int index = 0; index < eq->count; index++) {
        if (!parametric_filter_valid(&eq->filters[index])) {
            return false;
        }
    }
    return true;
}

static const int64_t UNIT = INT64_C(1) << PARAMETRIC_BITS;
static const int64_t PI = INT64_C(843314857);

static int64_t multiply(int64_t first, int64_t second)
{
    return first * second / UNIT;
}

int64_t parametric_cosine(int64_t angle)
{
    angle %= 2 * PI;
    angle = angle < 0 ? -angle : angle;
    angle = angle > PI ? 2 * PI - angle : angle;
    int sign = angle > PI / 2 ? -1 : 1;
    angle = angle > PI / 2 ? PI - angle : angle;
    int64_t square = multiply(angle, angle);
    int64_t term = UNIT;
    int64_t sum = term;
    for (int order = 2; order <= 16; order += 2) {
        term = -multiply(term, square) / (order * (order - 1));
        sum += term;
    }
    return sign * sum;
}

static uint32_t exponent(int tenths, int64_t up, int64_t down)
{
    int64_t value = INT64_C(1) << 30;
    int64_t step = tenths < 0 ? down : up;
    int count = tenths < 0 ? -tenths : tenths;
    for (int index = 0; index < count; index++) {
        value = (value * step + (INT64_C(1) << 29)) >> 30;
        if (value / 4 > UINT32_MAX) {
            return UINT32_MAX;
        }
    }
    return (uint32_t)((value + 2) / 4);
}

uint32_t parametric_amplitude(int tenths)
{
    return exponent(tenths, INT64_C(1086175168), INT64_C(1061450803));
}

static int64_t amplitude(int tenths, int divisor)
{
    int64_t unit = INT64_C(1) << 30;
    int64_t x = INT64_C(2472381918) * tenths / divisor;
    int64_t term = unit;
    int64_t sum = unit;
    for (int order = 1; order <= 14; order++) {
        term = term * x / unit / order;
        sum += term;
    }
    return (sum + 2) / 4;
}

static int32_t normalized(int64_t value, int64_t a0)
{
    int64_t scaled = value * UNIT;
    return (int32_t)((scaled + (scaled < 0 ? -a0 / 2 : a0 / 2)) / a0);
}

static ParametricSection shelf(const ParametricFilter *filter, int64_t a, int64_t c, int64_t beta)
{
    int sign = filter->type == PARAMETRIC_LOW_SHELF ? 1 : -1;
    int64_t sum = a + UNIT;
    int64_t difference = a - UNIT;
    int64_t dc = multiply(difference, c);
    int64_t sc = multiply(sum, c);
    int64_t a0 = sum + sign * dc + beta;
    return (ParametricSection){normalized(multiply(a, sum - sign * dc + beta), a0),
                               normalized(2 * sign * multiply(a, difference - sign * sc), a0),
                               normalized(multiply(a, sum - sign * dc - beta), a0),
                               normalized(2 * sign * (difference + sign * sc), a0),
                               normalized(-(sum + sign * dc - beta), a0)};
}

bool parametric_design(ParametricSection *section, const ParametricFilter *filter, uint32_t rate)
{
    if (!parametric_filter_valid(filter) || !filter->enabled || rate == 0 || rate > 96000 ||
        2u * filter->hz >= rate) {
        return false;
    }
    int64_t angle = 2 * PI * filter->hz / rate;
    int64_t cosine = parametric_cosine(angle);
    int64_t alpha = parametric_cosine(PI / 2 - angle) * 50 / filter->q;
    int64_t a = amplitude(filter->gain, 400);
    if (filter->type != PARAMETRIC_PEAK) {
        int64_t root = amplitude(filter->gain, 800);
        *section = shelf(filter, a, cosine, 2 * multiply(root, alpha));
    } else {
        int64_t a0 = UNIT + alpha * UNIT / a;
        *section = (ParametricSection){
            normalized(UNIT + multiply(alpha, a), a0), normalized(-2 * cosine, a0),
            normalized(UNIT - multiply(alpha, a), a0), normalized(2 * cosine, a0),
            normalized(-(UNIT - alpha * UNIT / a), a0)};
    }
    return true;
}

static uint64_t magnitude(int64_t real, int64_t imaginary)
{
    real /= 8;
    imaginary /= 8;
    return (uint64_t)(real * real) + (uint64_t)(imaginary * imaginary);
}

uint64_t parametric_power_at(const ParametricSection *section, int64_t c, int64_t s)
{
    uint64_t numerator = magnitude(multiply((int64_t)section->b0 + section->b2, c) + section->b1,
                                   multiply((int64_t)section->b0 - section->b2, s));
    uint64_t denominator =
        magnitude(multiply(UNIT - section->a2, c) - section->a1, multiply(UNIT + section->a2, s));
    while (numerator > (UINT64_MAX >> 20)) {
        numerator >>= 1;
        denominator >>= 1;
    }
    return denominator == 0 ? UINT64_C(1) << 50 : (numerator << 20) / denominator;
}

uint64_t parametric_power(const ParametricSection *section, uint32_t hz, uint32_t rate)
{
    int64_t angle = 2 * PI * hz / rate;
    return parametric_power_at(section, parametric_cosine(angle),
                               parametric_cosine(PI / 2 - angle));
}

int16_t parametric_decibels(uint64_t power)
{
    const uint64_t unit = UINT64_C(1) << 20;
    bool negative = power < unit;
    power = power == 0 ? 1 : power;
    power = negative ? unit * unit / power : power;
    int db = 0;
    while (power > unit && db < 1200) {
        power = power * 1024707 >> 20;
        db++;
    }
    if (db > 0 && power < 1036573) {
        db--;
    }
    return (int16_t)(negative ? -db : db);
}
