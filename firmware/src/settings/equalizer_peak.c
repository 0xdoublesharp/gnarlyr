#include "equalizer_peak.h"

enum {
    GRID_POINTS = 78,
    GAIN_STEPS = 2 * SETTINGS_EQUALIZER_GAIN_MAX + 1,
    UNIT_BITS = 30,
    RATIO_BITS = 20,
    PRE_GAIN_BITS = 15,
    ROOT_TO_PRE_GAIN_BITS = 25,
    HEADROOM_STEPS = 4 * SETTINGS_EQUALIZER_GAIN_MAX + 1,
    HALF_DB_POWER_RATIO = 934541,
    TROUGH_MARGIN_BITS = 7
};

static const uint32_t BAND_TANGENT[SETTINGS_EQUALIZER_BANDS] = {
    2316, 4631, 9338, 18677, 37365, 74825, 150416, 307152, 671961, 2280410};
static const uint32_t GRID_TANGENT[GRID_POINTS] = {
    2316,    3287,    3586,    3884,    4258,    4631,    5005,    5453,   5976,   6499,
    7096,    7769,    8441,    9188,    9338,    10085,   10981,   11952,  12998,  14194,
    15464,   16883,   18452,   18677,   20096,   21890,   26075,   28467,  31009,  33850,
    36916,   37365,   40207,   43874,   47840,   52182,   56901,   62072,  67696,  73849,
    74825,   80533,   87901,   95880,   104624,  114139,  124581,  135958, 148434, 150416,
    162099,  177046,  193376,  211352,  231014,  252729,  276566,  302939, 307152, 332052,
    364399,  400369,  440517,  485689,  536631,  594588,  661054,  671961, 738236, 828904,
    1070008, 1236528, 1454105, 1752751, 2194160, 2280410, 2927573, 4419613};
static const uint32_t POWER[GAIN_STEPS] = {
    339546978,  359666402,  380977976,  403552340,  427464319,  452793173,  479622855,
    508042296,  538145694,  570032831,  603809400,  639587356,  677485290,  717628817,
    760150998,  805192776,  852903448,  903441154,  956973408,  1013677647, 1073741824,
    1137365027, 1204758142, 1276144550, 1351760868, 1431857735, 1516700640, 1606570803,
    1701766107, 1802602089, 1909412977, 2022552809, 2142396597, 2269341577, 2403808519,
    2546243128, 2697117519, 2856931779, 3026215629, 3205530179, 3395469783};
static const uint16_t HEADROOM[HEADROOM_STEPS] = {
    26029, 25510, 25001, 24502, 24013, 23534, 23065, 22605, 22154, 21712, 21279,
    20855, 20439, 20031, 19631, 19240, 18856, 18480, 18111, 17750, 17396, 17049,
    16709, 16376, 16049, 15729, 15415, 15108, 14806, 14511, 14222, 13938, 13660,
    13387, 13120, 12859, 12602, 12351, 12104, 11863, 11626};

static uint64_t one(int bits)
{
    return UINT64_C(1) << bits;
}

static uint64_t squared_ratio(uint32_t grid, uint32_t band)
{
    uint64_t low = grid < band ? grid : band;
    uint64_t high = grid < band ? band : grid;
    uint64_t ratio = (low << UNIT_BITS) / high;
    return ratio * ratio >> UNIT_BITS;
}

static uint64_t power_of(int gain)
{
    return 2 * (uint64_t)POWER[SETTINGS_EQUALIZER_GAIN_MAX + gain];
}

static uint64_t band_power(uint64_t squared, int gain)
{
    uint64_t flat = one(UNIT_BITS) - squared;
    flat = flat * flat >> UNIT_BITS;
    uint64_t numerator = flat + (power_of(gain) * squared >> UNIT_BITS);
    uint64_t denominator = flat + (power_of(-gain) * squared >> UNIT_BITS);
    return (numerator << RATIO_BITS) / denominator;
}

uint32_t equalizer_peak_grid_tangent(int point)
{
    return GRID_TANGENT[point];
}

uint64_t equalizer_peak_cascade(const int8_t *gains, const uint32_t *tangents, int count,
                                uint32_t grid)
{
    uint64_t power = one(RATIO_BITS);
    for (int band = 0; band < count; band++) {
        if (gains[band] != 0) {
            uint64_t squared = squared_ratio(grid, tangents[band]);
            power = power * band_power(squared, gains[band]) >> RATIO_BITS;
        }
    }
    return power;
}

static uint64_t cascade_power(const int8_t *gains, uint32_t grid)
{
    return equalizer_peak_cascade(gains, BAND_TANGENT, SETTINGS_EQUALIZER_BANDS, grid);
}

static uint64_t peak_power(const int8_t *gains)
{
    uint64_t peak = one(RATIO_BITS);
    for (int point = 0; point < GRID_POINTS; point++) {
        uint64_t power = cascade_power(gains, GRID_TANGENT[point]);
        peak = power > peak ? power : peak;
    }
    return peak;
}

static uint32_t square_root(uint64_t value)
{
    uint64_t root = 0;
    uint64_t bit = one(62);
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

static uint64_t trough_power(const int8_t *gains)
{
    uint64_t trough = one(RATIO_BITS);
    for (int point = 0; point < GRID_POINTS; point++) {
        uint64_t power = cascade_power(gains, GRID_TANGENT[point]);
        trough = power < trough ? power : trough;
    }
    return trough;
}

static int deepest_cut(const int8_t *gains)
{
    uint64_t trough = trough_power(gains);
    uint64_t level = one(RATIO_BITS);
    int cut = 0;
    trough += trough >> TROUGH_MARGIN_BITS;
    while (cut < HEADROOM_STEPS - 1 && trough < level) {
        level = level * HALF_DB_POWER_RATIO >> RATIO_BITS;
        cut++;
    }
    return cut;
}

uint16_t equalizer_peak_pre_gain(const int8_t gains[SETTINGS_EQUALIZER_BANDS])
{
    uint32_t pre_gain = (UINT32_C(1) << ROOT_TO_PRE_GAIN_BITS) / square_root(peak_power(gains));
    return (uint16_t)(pre_gain * HEADROOM[deepest_cut(gains)] >> PRE_GAIN_BITS);
}
