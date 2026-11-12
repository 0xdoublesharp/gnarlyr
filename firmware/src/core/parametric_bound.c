#include "core/parametric.h"

static const int64_t UNIT = INT64_C(1) << PARAMETRIC_BITS;
static const uint64_t BOUND_LIMIT = UINT64_C(1) << 52;

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

static uint64_t geometric(uint64_t value, uint64_t pole)
{
    if (pole >= (uint64_t)UNIT) {
        return BOUND_LIMIT;
    }
    uint64_t divisor = (uint64_t)UNIT - pole;
    if (value / divisor >= BOUND_LIMIT / (uint64_t)UNIT) {
        return BOUND_LIMIT;
    }
    uint64_t bound = value / divisor * (uint64_t)UNIT +
                     (value % divisor * (uint64_t)UNIT + divisor - 1) / divisor;
    return bound < BOUND_LIMIT ? bound : BOUND_LIMIT;
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
            return BOUND_LIMIT;
        }
        return geometric(absolute(c1), absolute(first)) + geometric(absolute(c2), absolute(second));
    }
    uint64_t radius = root((uint64_t)(-(int64_t)k->a2 * UNIT));
    uint64_t imaginary = root((uint64_t)(-discriminant)) / 2;
    if (imaginary == 0 || radius >= (uint64_t)UNIT) {
        return BOUND_LIMIT;
    }
    uint64_t envelope = absolute(h2) + absolute(h3 * UNIT - (int64_t)k->a1 * h2 / 2) / imaginary;
    return geometric(envelope, radius);
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

enum {
    IMPULSE_BITS = 24,
    IMPULSE_UNIT = 1 << IMPULSE_BITS,
    IMPULSE_BLOCK = 64,
    IMPULSE_SAMPLES = 4096,
    HISTORY_LIMIT = 1 << 29
};

typedef struct ImpulseHistory {
    int64_t x1;
    int64_t x2;
    int64_t y1;
    int64_t y2;
    int64_t error;
    uint64_t sum;
} ImpulseHistory;

typedef struct ImpulseBound {
    uint64_t section;
    uint64_t gradient;
    uint64_t denominator;
} ImpulseBound;

static uint64_t product(uint64_t first, uint64_t second)
{
    uint64_t high = first >> PARAMETRIC_BITS;
    if (high != 0 && second > BOUND_LIMIT / high) {
        return BOUND_LIMIT;
    }
    uint64_t low = first & ((uint64_t)UNIT - 1);
    uint64_t result =
        high * second + low * (second >> PARAMETRIC_BITS) +
        ((low * (second & ((uint64_t)UNIT - 1)) + (uint64_t)UNIT - 1) >> PARAMETRIC_BITS);
    return result < BOUND_LIMIT ? result : BOUND_LIMIT;
}

static int64_t impulse_step(const ParametricSection *k, ImpulseHistory *h, int64_t x)
{
    int64_t sum = h->error + (int64_t)k->b0 * x + (int64_t)k->b1 * h->x1 + (int64_t)k->b2 * h->x2 +
                  (int64_t)k->a1 * h->y1 + (int64_t)k->a2 * h->y2;
    int64_t y = sum >> PARAMETRIC_BITS;
    h->error = sum - y * UNIT;
    h->x2 = h->x1;
    h->x1 = x;
    h->y2 = h->y1;
    h->y1 = y;
    h->sum += absolute(y);
    return y;
}

static uint64_t remaining(const ParametricSection *k, const ImpulseHistory *h)
{
    int64_t first = ((int64_t)k->b1 * h->x1 + (int64_t)k->b2 * h->x2 + (int64_t)k->a1 * h->y1 +
                     (int64_t)k->a2 * h->y2 + h->error) /
                    UNIT;
    if (absolute(first) > HISTORY_LIMIT) {
        return BOUND_LIMIT;
    }
    int64_t second =
        ((int64_t)k->b2 * h->x1 + (int64_t)k->a1 * first + (int64_t)k->a2 * h->y1) / UNIT;
    return absolute(second) > HISTORY_LIMIT ? BOUND_LIMIT : tail_bound(k, first, second);
}

static bool section_bound(ImpulseBound *bound, const ParametricSection *section)
{
    bound->section = parametric_bound(section);
    ParametricSection noise = {
        .b0 = (int32_t)UNIT, .b1 = -(int32_t)UNIT, .a1 = section->a1, .a2 = section->a2};
    bound->gradient = parametric_bound(&noise);
    noise.b1 = 0;
    bound->denominator = parametric_bound(&noise);
    return bound->section < BOUND_LIMIT;
}

static uint16_t prefix_gain(uint64_t bound, const ImpulseBound *noise, uint32_t frames)
{
    uint64_t error =
        ((uint64_t)frames * noise->gradient + 4 * noise->denominator + (uint64_t)UNIT - 1) >>
        PARAMETRIC_BITS;
    uint64_t pcm_error = (32 * noise->gradient + 3 * noise->denominator + 16 * (uint64_t)UNIT - 1) /
                         (16 * (uint64_t)UNIT);
    bound += error;
    bound += bound / 256 + 8;
    if (pcm_error >= IMPULSE_UNIT) {
        return 0;
    }
    uint64_t gain = UINT64_C(32768) * (IMPULSE_UNIT - pcm_error) / bound;
    return gain < 32768 ? (uint16_t)gain : 32768;
}

uint16_t parametric_cascade_pre_gain(const ParametricSection *sections, int count)
{
    if (count < 0 || count > PARAMETRIC_FILTERS) {
        return 0;
    }
    ImpulseHistory histories[PARAMETRIC_FILTERS] = {0};
    ImpulseBound bounds[PARAMETRIC_FILTERS];
    ImpulseBound errors[PARAMETRIC_FILTERS];
    uint64_t fallback = 32768;
    ImpulseBound error = {0};
    for (int index = 0; index < count; index++) {
        if (!section_bound(&bounds[index], &sections[index])) {
            return 0;
        }
        uint64_t bound = bounds[index].section;
        fallback = fallback * (uint64_t)UNIT / (bound > (uint64_t)UNIT ? bound : (uint64_t)UNIT);
        error.gradient = product(error.gradient, bound) + bounds[index].gradient;
        error.denominator = product(error.denominator, bound) + bounds[index].denominator;
        errors[index] = error;
        if (error.gradient >= BOUND_LIMIT || error.denominator >= BOUND_LIMIT) {
            return 0;
        }
    }
    if (count == 0) {
        return 32768;
    }
    uint16_t best = (uint16_t)(fallback * parametric_amplitude(-20) >> PARAMETRIC_BITS);
    for (uint32_t frame = 0; frame < IMPULSE_SAMPLES; frame++) {
        int64_t value = frame == 0 ? IMPULSE_UNIT : 0;
        for (int index = 0; index < count; index++) {
            value = impulse_step(&sections[index], &histories[index], value);
            if (absolute(value) > HISTORY_LIMIT) {
                return best;
            }
        }
        if ((frame + 1) % IMPULSE_BLOCK != 0) {
            continue;
        }
        uint64_t tail = 0;
        uint64_t largest_tail = 0;
        uint16_t gain = 32768;
        for (int index = 0; index < count; index++) {
            tail = product(tail, bounds[index].section) +
                   remaining(&sections[index], &histories[index]);
            if (tail >= BOUND_LIMIT) {
                return best;
            }
            uint16_t prefix = prefix_gain(histories[index].sum + tail, &errors[index], frame + 1);
            gain = prefix < gain ? prefix : gain;
            largest_tail = tail > largest_tail ? tail : largest_tail;
        }
        best = gain > best ? gain : best;
        if (largest_tail < IMPULSE_UNIT / 4096) {
            break;
        }
    }
    return best;
}
