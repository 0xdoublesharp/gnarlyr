#include "settings/equalizer_response.h"

#include "equalizer_peak.h"

enum {
    POWER_BITS = 20,
    HALF_DB_POWER = 934541,
    TENTHS_PER_HALF_DB = 5,
    MIN_RANGE_TENTH_DB = 100,
    RANGE_STEP_TENTH_DB = 50,
    KNOWN_BANDS = 15,
    REFERENCE_RATE = 44100,
    LOWEST_RATE_PER_HZ = 4
};

static const uint16_t GRID_HZ[EQUALIZER_RESPONSE_POINTS] = {
    31,   44,   48,   52,   57,   62,    67,    73,    80,    87,    95,    104,   113,
    123,  125,  135,  147,  160,  174,   190,   207,   226,   247,   250,   269,   293,
    349,  381,  415,  453,  494,  500,   538,   587,   640,   698,   761,   830,   905,
    987,  1000, 1076, 1174, 1280, 1396,  1522,  1660,  1810,  1974,  2000,  2153,  2348,
    2560, 2792, 3044, 3320, 3620, 3948,  4000,  4305,  4695,  5120,  5583,  6089,  6640,
    7241, 7896, 8000, 8611, 9390, 11167, 12177, 13280, 14482, 15792, 16000, 17222, 18780};
static const uint16_t BAND_HZ[KNOWN_BANDS] = {31,   62,   63,   100,  125,  250,   315,  500,
                                              1000, 2000, 3150, 4000, 8000, 10000, 16000};
static const uint32_t BAND_TANGENT[KNOWN_BANDS] = {2316,   4631,   4706,   7470,   9338,
                                                   18677,  23534,  37365,  74825,  150416,
                                                   239331, 307152, 671961, 905628, 2280410};

uint16_t equalizer_response_frequency(int point)
{
    return GRID_HZ[point];
}

static uint32_t tangent_for(uint16_t hz)
{
    for (int band = 0; band < KNOWN_BANDS; band++) {
        if (BAND_HZ[band] == hz) {
            return BAND_TANGENT[band];
        }
    }
    return 0;
}

static int16_t tenth_db(uint64_t power)
{
    const uint64_t unit = UINT64_C(1) << POWER_BITS;
    bool negative = power < unit;
    power = power == 0 ? 1 : power;
    power = negative ? unit * unit / power : power;
    int half_db = 0;
    while (power > unit) {
        power = power * HALF_DB_POWER >> POWER_BITS;
        half_db++;
    }
    int remainder = (int)(((unit - power) * TENTHS_PER_HALF_DB + (unit - HALF_DB_POWER) / 2) /
                          (unit - HALF_DB_POWER));
    int result = half_db * TENTHS_PER_HALF_DB - remainder;
    return (int16_t)(negative ? -result : result);
}

static bool band_tangents(const SettingsEqualizerCurve *curve, uint32_t *tangents)
{
    if (curve->band_count > SETTINGS_EQUALIZER_BANDS || curve->pre_gain == 0) {
        return false;
    }
    for (int band = 0; band < curve->band_count; band++) {
        tangents[band] = tangent_for(curve->band_hz[band]);
        int gain = curve->gain_half_db[band];
        if (tangents[band] == 0 || gain < -SETTINGS_EQUALIZER_GAIN_MAX ||
            gain > SETTINGS_EQUALIZER_GAIN_MAX) {
            return false;
        }
    }
    return true;
}

bool equalizer_response_make(EqualizerResponse *response, const SettingsEqualizerCurve *curve)
{
    if (curve->is_parametric) {
        *response = (EqualizerResponse){.range_tenth_db = MIN_RANGE_TENTH_DB};
        ParametricSection sections[PARAMETRIC_FILTERS];
        int count = 0;
        for (int index = 0; index < curve->parametric.count && index < PARAMETRIC_FILTERS;
             index++) {
            if (parametric_design(&sections[count], &curve->parametric.filters[index],
                                  REFERENCE_RATE)) {
                count++;
            }
        }
        uint64_t frequency = UINT64_C(20) << 16;
        for (int point = 0; point < EQUALIZER_RESPONSE_POINTS; point++) {
            response->frequency_hz[point] = point < 68 ? (uint16_t)(frequency >> 16)
                                            : point - 68 < curve->parametric.count
                                                ? curve->parametric.filters[point - 68].hz
                                                : 20000;
            frequency = frequency * 72653 >> 16;
        }
        response->frequency_hz[67] = 20000;
        for (int point = 1; point < EQUALIZER_RESPONSE_POINTS; point++) {
            uint16_t hz = response->frequency_hz[point];
            int before = point;
            while (before > 0 && hz < response->frequency_hz[before - 1]) {
                response->frequency_hz[before] = response->frequency_hz[before - 1];
                before--;
            }
            response->frequency_hz[before] = hz;
        }
        for (int point = 0; point < EQUALIZER_RESPONSE_POINTS; point++) {
            int16_t db = (int16_t)parametric_cascade_response(
                sections, count, response->frequency_hz[point], REFERENCE_RATE);
            response->gain_tenth_db[point] = db;
            int magnitude = db < 0 ? -db : db;
            int range =
                (magnitude + RANGE_STEP_TENTH_DB - 1) / RANGE_STEP_TENTH_DB * RANGE_STEP_TENTH_DB;
            response->range_tenth_db =
                range > response->range_tenth_db ? (int16_t)range : response->range_tenth_db;
        }
        uint64_t gain = parametric_pre_gain(&curve->parametric, REFERENCE_RATE);
        response->preamp_tenth_db = parametric_decibels(gain * gain >> 10);
        response->muted = gain == 0;
        return true;
    }
    uint32_t tangents[SETTINGS_EQUALIZER_BANDS];
    if (!band_tangents(curve, tangents)) {
        return false;
    }
    int8_t active_gains[SETTINGS_EQUALIZER_BANDS];
    for (int band = 0; band < curve->band_count; band++) {
        active_gains[band] = curve->band_hz[band] < REFERENCE_RATE / LOWEST_RATE_PER_HZ
                                 ? curve->gain_half_db[band]
                                 : 0;
    }
    *response = (EqualizerResponse){.range_tenth_db = MIN_RANGE_TENTH_DB};
    for (int point = 0; point < EQUALIZER_RESPONSE_POINTS; point++) {
        uint64_t power = equalizer_peak_cascade(active_gains, tangents, curve->band_count,
                                                equalizer_peak_grid_tangent(point));
        int16_t db = tenth_db(power);
        response->gain_tenth_db[point] = db;
        int magnitude = db < 0 ? -db : db;
        int range =
            (magnitude + RANGE_STEP_TENTH_DB - 1) / RANGE_STEP_TENTH_DB * RANGE_STEP_TENTH_DB;
        response->range_tenth_db =
            range > response->range_tenth_db ? (int16_t)range : response->range_tenth_db;
    }
    response->preamp_tenth_db = tenth_db((uint64_t)curve->pre_gain * curve->pre_gain >> 10);
    return true;
}
