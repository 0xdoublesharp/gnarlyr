#include <math.h>
#include <stddef.h>

#include "check.h"
#include "device/dac_eq.h"

enum {
    SAMPLE_RATE = 48000,
    COEFFICIENT_SCALE = 32768,
    SOFTWARE_SCALE = 1 << EQUALIZER_COEFFICIENT_BITS,
    SOFTWARE_UNITS_PER_DAC_STEP = SOFTWARE_SCALE / COEFFICIENT_SCALE,
    WRITE_COUNT = 2 * (54 + 2)
};

static const double PI = 3.14159265358979323846;

static Equalizer design_curve(int bands, uint32_t rate)
{
    Equalizer eq = {0};
    HalAudioEqualizer curve = {.band_hz = {1000, 2000, 4000, 8000},
                               .gain_half_db = {8, -6, -4, 4},
                               .band_count = (uint8_t)bands,
                               .pre_gain = HAL_AUDIO_UNITY_PRE_GAIN * 3 / 4};
    CHECK_EQUAL(bands != 0, equalizer_design(&eq, &curve, rate));
    CHECK_EQUAL(bands, eq.section_count);
    return eq;
}

static void bypass_and_more_than_three_sections_fall_back(void)
{
    DacEqProgram out = {.enabled = true};
    Equalizer eq = design_curve(0, SAMPLE_RATE);
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);

    out.enabled = true;
    CHECK(!dac_eq_from_equalizer(NULL, SAMPLE_RATE, &out));
    CHECK(!out.enabled);

    eq = design_curve(4, SAMPLE_RATE);
    out.enabled = true;
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);
}

static void only_active_sos_stages_are_enabled(void)
{
    const uint8_t control1[] = {0xC0, 0xF0, 0xF2};
    const uint8_t control2[] = {0x08, 0x0C, 0x0C};
    for (int bands = 1; bands <= 3; bands++) {
        Equalizer eq = design_curve(bands, SAMPLE_RATE);
        DacEqProgram out;
        CHECK(dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
        CHECK(out.enabled);
        CHECK_EQUAL(control1[bands - 1], out.control1);
        CHECK_EQUAL(control2[bands - 1], out.control2);
        for (size_t i = (size_t)bands * 15; i < sizeof out.coeffs; i++) {
            CHECK_EQUAL(0, out.coeffs[i]);
        }
    }
}

static void coefficients_follow_register_order_and_feedback_signs(void)
{
    Equalizer eq = {.section_count = 3,
                    .sections = {
                        {SOFTWARE_SCALE, -3 * SOFTWARE_SCALE / 2, SOFTWARE_SCALE / 2,
                         SOFTWARE_SCALE / 2, -SOFTWARE_SCALE / 4},
                        {SOFTWARE_SCALE / 4, -SOFTWARE_SCALE / 2, -SOFTWARE_SCALE,
                         -SOFTWARE_SCALE / 4, SOFTWARE_SCALE / 8},
                        {SOFTWARE_SCALE / 8, SOFTWARE_SCALE / 2, SOFTWARE_SCALE / 4,
                         SOFTWARE_SCALE / 4, -SOFTWARE_SCALE / 2},
                    }};
    const uint8_t expected[54] = {
        0x00, 0x80, 0, 0x00, 0x40, 1, 0x00, 0x40, 0, 0x00, 0xC0, 1, 0x00, 0x20, 0,
        0x00, 0x20, 0, 0x00, 0xC0, 1, 0x00, 0x80, 1, 0x00, 0x20, 0, 0x00, 0xF0, 1,
        0x00, 0x10, 0, 0x00, 0x40, 0, 0x00, 0x20, 0, 0x00, 0xE0, 1, 0x00, 0x40, 0,
    };
    DacEqProgram out;
    CHECK(dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    for (size_t i = 0; i < sizeof expected; i++) {
        CHECK_EQUAL(expected[i], out.coeffs[i]);
    }
}

static void coefficients_round_to_the_nearest_step_and_must_fit(void)
{
    const int32_t max = 2 * SOFTWARE_SCALE - SOFTWARE_UNITS_PER_DAC_STEP;
    const int32_t half_step = SOFTWARE_UNITS_PER_DAC_STEP / 2;
    Equalizer eq = {.section_count = 1,
                    .sections = {{max, -2 * SOFTWARE_SCALE, SOFTWARE_SCALE, 0, half_step}}};
    DacEqProgram out;
    CHECK(dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    const uint8_t expected[15] = {0xFF, 0xFF, 0, 0x00, 0x00, 1,    0x00, 0x80,
                                  0,    0x00, 0, 0,    0xFF, 0xFF, 1};
    for (size_t i = 0; i < sizeof expected; i++) {
        CHECK_EQUAL(expected[i], out.coeffs[i]);
    }

    eq.sections[0].b0 = max + 1;
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);
    eq.sections[0].b0 = max;
    eq.sections[0].b1 = -2 * SOFTWARE_SCALE - 1;
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);
    eq.sections[0].b1 = 0;
    eq.sections[0].b2 = -half_step;
    CHECK(dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK_EQUAL(0xFF, out.coeffs[6]);
    CHECK_EQUAL(0xFF, out.coeffs[7]);
    CHECK_EQUAL(1, out.coeffs[8]);
    eq.sections[0].a1 = INT32_MIN;
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);
}

static void rounding_a_pole_onto_the_unit_circle_falls_back(void)
{
    Equalizer eq = {.section_count = 1,
                    .sections = {{.b0 = SOFTWARE_SCALE,
                                  .a1 = SOFTWARE_SCALE - SOFTWARE_UNITS_PER_DAC_STEP / 4}}};
    DacEqProgram out = {.enabled = true};
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);
}

static void excessive_dc_gain_error_falls_back(void)
{
    Equalizer eq = {0};
    const HalAudioEqualizer curve = {.band_hz = {62},
                                     .gain_half_db = {-8},
                                     .band_count = 1,
                                     .pre_gain = HAL_AUDIO_UNITY_PRE_GAIN * 3 / 4};
    DacEqProgram out = {.enabled = true};
    CHECK(equalizer_design(&eq, &curve, SAMPLE_RATE));
    CHECK(!dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
    CHECK(!out.enabled);
}

static void only_compatible_echo_rates_allow_hardware_eq(void)
{
    const uint32_t supported[] = {32000, 44100, 48000, 88200, 96000, 176400};
    Equalizer eq = {.section_count = 1, .sections = {{.b0 = SOFTWARE_SCALE}}};
    DacEqProgram out;
    for (size_t i = 0; i < sizeof supported / sizeof supported[0]; i++) {
        CHECK(dac_eq_from_equalizer(&eq, supported[i], &out));
    }
    const uint32_t unavailable[] = {0, 16000, 192000, 384000};
    for (size_t i = 0; i < sizeof unavailable / sizeof unavailable[0]; i++) {
        CHECK(!dac_eq_from_equalizer(&eq, unavailable[i], &out));
        CHECK(!out.enabled);
    }
}

static int32_t decode_coefficient(const uint8_t *bytes)
{
    return bytes[0] + 256 * bytes[1] - 65536 * bytes[2];
}

static double polynomial_magnitude(double c0, double c1, double c2, double radians)
{
    return hypot(c0 + c1 * cos(radians) + c2 * cos(2 * radians),
                 c1 * sin(radians) + c2 * sin(2 * radians));
}

static double software_magnitude(const EqualizerSection *section, double radians)
{
    return polynomial_magnitude(section->b0, section->b1, section->b2, radians) /
           polynomial_magnitude(SOFTWARE_SCALE, -section->a1, -section->a2, radians);
}

static double hardware_magnitude(const uint8_t *bytes, double radians)
{
    return polynomial_magnitude(decode_coefficient(bytes), decode_coefficient(bytes + 3),
                                decode_coefficient(bytes + 6), radians) /
           polynomial_magnitude(COEFFICIENT_SCALE, decode_coefficient(bytes + 9),
                                decode_coefficient(bytes + 12), radians);
}

static void quantized_response_matches_the_software_cascade(void)
{
    const uint32_t frequencies[] = {0, 1000, 2000, 4000, SAMPLE_RATE / 8, SAMPLE_RATE / 4};
    for (int bands = 1; bands <= 3; bands++) {
        Equalizer eq = design_curve(bands, SAMPLE_RATE);
        DacEqProgram out = {0};
        CHECK(dac_eq_from_equalizer(&eq, SAMPLE_RATE, &out));
        for (size_t i = 0; i < sizeof frequencies / sizeof frequencies[0]; i++) {
            double radians = 2 * PI * frequencies[i] / SAMPLE_RATE;
            double software = 1;
            double hardware = 1;
            for (int band = 0; band < bands; band++) {
                software *= software_magnitude(&eq.sections[band], radians);
                hardware *= hardware_magnitude(out.coeffs + band * 15, radians);
            }
            CHECK(fabs(20 * log10(hardware / software)) < 0.1);
        }
    }
}

typedef struct Write {
    uint8_t address;
    uint32_t map;
    uint8_t value;
} Write;

typedef struct RecordingBus {
    Write writes[WRITE_COUNT + 4];
    int count;
    int fail_at;
} RecordingBus;

static bool record_write(void *context, uint8_t address, uint32_t map, uint8_t value)
{
    RecordingBus *record = context;
    int index = record->count++;
    CHECK(index < WRITE_COUNT + 4);
    if (index >= WRITE_COUNT + 4) {
        return false;
    }
    record->writes[index] = (Write){address, map, value};
    return record->count != record->fail_at;
}

static void check_pair(const RecordingBus *record, int index, uint32_t map, uint8_t value)
{
    CHECK_EQUAL(0x60, record->writes[index].address);
    CHECK_EQUAL(0x66, record->writes[index + 1].address);
    for (int chip = 0; chip < 2; chip++) {
        CHECK_EQUAL(map, record->writes[index + chip].map);
        CHECK_EQUAL(value, record->writes[index + chip].value);
    }
}

static void program_writes_every_coefficient_before_enabling_both_chips(void)
{
    DacEqProgram program = {.enabled = true, .control1 = 0xF2, .control2 = 0x0C};
    for (size_t i = 0; i < sizeof program.coeffs; i++) {
        program.coeffs[i] = (uint8_t)i;
    }
    RecordingBus record = {0};
    DacBus bus = {.context = &record, .write = record_write};
    CHECK(dac_eq_write(&bus, &program));
    CHECK_EQUAL(WRITE_COUNT, record.count);
    for (int i = 0; i < 54; i++) {
        check_pair(&record, 2 * i, 0x09000C + (uint32_t)i, program.coeffs[i]);
    }
    check_pair(&record, 108, 0x09000B, 0x0C);
    check_pair(&record, 110, 0x09000A, 0xF2);
}

static void disable_clears_both_controls_on_both_chips(void)
{
    RecordingBus record = {0};
    DacBus bus = {.context = &record, .write = record_write};
    CHECK(dac_eq_disable(&bus));
    CHECK_EQUAL(4, record.count);
    check_pair(&record, 0, 0x09000A, 0);
    check_pair(&record, 2, 0x09000B, 0);

    const DacEqProgram disabled = {0};
    record.count = 0;
    CHECK(dac_eq_write(&bus, &disabled));
    CHECK_EQUAL(4, record.count);
    check_pair(&record, 0, 0x09000A, 0);
    check_pair(&record, 2, 0x09000B, 0);

    record.count = 0;
    record.fail_at = 1;
    CHECK(!dac_eq_disable(&bus));
    CHECK_EQUAL(4, record.count);
    check_pair(&record, 0, 0x09000A, 0);
    check_pair(&record, 2, 0x09000B, 0);
}

static void a_failed_program_attempt_disables_both_chips(void)
{
    const DacEqProgram program = {.enabled = true, .control1 = 0xC0, .control2 = 0x08};
    const int failed_transfers[] = {1, 2, 109, 110, 111, 112};
    for (size_t i = 0; i < sizeof failed_transfers / sizeof failed_transfers[0]; i++) {
        const int end_of_pair = (failed_transfers[i] + 1) / 2 * 2;
        RecordingBus record = {.fail_at = failed_transfers[i]};
        DacBus bus = {.context = &record, .write = record_write};
        CHECK(!dac_eq_write(&bus, &program));
        CHECK_EQUAL(end_of_pair + 4, record.count);
        check_pair(&record, end_of_pair, 0x09000A, 0);
        check_pair(&record, end_of_pair + 2, 0x09000B, 0);
    }
}

static void wideband_requires_192_khz_an_oversampling_filter_and_hardware_eq_off(void)
{
    const uint32_t rates[] = {0, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 384000};
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        for (uint8_t filter = 0; filter < DAC_ANALOG_FILTERS; filter++) {
            CHECK_EQUAL(rates[i] == 192000 && filter < 4,
                        dac_wideband_flatness_eligible(rates[i], filter, false));
            CHECK(!dac_wideband_flatness_eligible(rates[i], filter, true));
        }
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(bypass_and_more_than_three_sections_fall_back),
        TEST_CASE(only_active_sos_stages_are_enabled),
        TEST_CASE(coefficients_follow_register_order_and_feedback_signs),
        TEST_CASE(coefficients_round_to_the_nearest_step_and_must_fit),
        TEST_CASE(rounding_a_pole_onto_the_unit_circle_falls_back),
        TEST_CASE(excessive_dc_gain_error_falls_back),
        TEST_CASE(only_compatible_echo_rates_allow_hardware_eq),
        TEST_CASE(quantized_response_matches_the_software_cascade),
        TEST_CASE(program_writes_every_coefficient_before_enabling_both_chips),
        TEST_CASE(disable_clears_both_controls_on_both_chips),
        TEST_CASE(a_failed_program_attempt_disables_both_chips),
        TEST_CASE(wideband_requires_192_khz_an_oversampling_filter_and_hardware_eq_off),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
