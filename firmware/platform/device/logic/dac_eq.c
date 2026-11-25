#include "device/dac_eq.h"

#include "device/audio_math.h"

enum {
    FILTER_CONTROL_1 = 0x09000A,
    FILTER_CONTROL_2 = 0x09000B,
    FILTER_COEFFICIENT_FIRST = 0x09000C,
    SOS1_ENABLE = 0xC0,
    SOS2_ENABLE = 0x30,
    SOS3_ENABLE = 0x02,
    SOS1_CONTROL_PORT = 0x08,
    SOS2_CONTROL_PORT = 0x04,
    SOS_COUNT = 3,
    WIDEBAND_RATE = 192000,
    NOS_FILTER_INDEX = 4,
    DAC_COEFFICIENT_ONE = 1 << 15,
    SOFTWARE_COEFFICIENT_ONE = 1 << EQUALIZER_COEFFICIENT_BITS,
    COEFFICIENT_STEP = SOFTWARE_COEFFICIENT_ONE / DAC_COEFFICIENT_ONE,
    COEFFICIENT_MIN = -2 * SOFTWARE_COEFFICIENT_ONE,
    COEFFICIENT_MAX = 2 * SOFTWARE_COEFFICIENT_ONE - COEFFICIENT_STEP,
    DC_GAIN_ERROR_DIVISOR = 88,
    B0 = 0,
    B1,
    B2,
    A1,
    A2,
    SOS_COEFFICIENTS,
    COEFFICIENT_BYTES = 3,
    SOS_BYTES = SOS_COEFFICIENTS * COEFFICIENT_BYTES
};

static bool dc_gain_matches(const int32_t *source, const int32_t *encoded, int section_count)
{
    int32_t source_denominator = SOFTWARE_COEFFICIENT_ONE + source[A1] + source[A2];
    int32_t encoded_denominator = DAC_COEFFICIENT_ONE + encoded[A1] + encoded[A2];
    int64_t expected = (int64_t)(source[B0] + source[B1] + source[B2]) * encoded_denominator;
    int64_t actual = (int64_t)(encoded[B0] + encoded[B1] + encoded[B2]) * source_denominator;
    int64_t error = actual - expected;
    if (error < 0) {
        error = -error;
    }
    if (expected < 0) {
        expected = -expected;
    }
    return error * (DC_GAIN_ERROR_DIVISOR * section_count) <= expected;
}

static bool encode_section(const EqualizerSection *section, int section_count, uint8_t *bytes)
{
    int32_t source[SOS_COEFFICIENTS] = {section->b0, section->b1, section->b2, section->a1,
                                        section->a2};
    int32_t encoded[SOS_COEFFICIENTS];
    for (int index = 0; index < SOS_COEFFICIENTS; index++) {
        int32_t value = source[index];
        if (index >= A1) {
            if (value < -COEFFICIENT_MAX || value > -COEFFICIENT_MIN) {
                return false;
            }
            source[index] = value = -value;
        }
        if (value < COEFFICIENT_MIN || value > COEFFICIENT_MAX) {
            return false;
        }
        int32_t half = COEFFICIENT_STEP / 2;
        encoded[index] = (value + (value < 0 ? -half : half)) / COEFFICIENT_STEP;
    }

    int32_t pole_limit = DAC_COEFFICIENT_ONE + encoded[A2];
    if (encoded[A2] >= DAC_COEFFICIENT_ONE || encoded[A1] <= -pole_limit ||
        encoded[A1] >= pole_limit || !dc_gain_matches(source, encoded, section_count)) {
        return false;
    }

    for (int index = 0; index < SOS_COEFFICIENTS; index++) {
        uint32_t bits = (uint32_t)encoded[index];
        bytes[0] = (uint8_t)bits;
        bytes[1] = (uint8_t)(bits >> 8);
        bytes[2] = (uint8_t)((bits >> 16) & 1u);
        bytes += COEFFICIENT_BYTES;
    }
    return true;
}

bool dac_eq_from_equalizer(const Equalizer *eq, uint32_t sample_rate, DacEqProgram *out)
{
    *out = (DacEqProgram){0};
    if (!eq || eq->section_count < 1 || eq->section_count > SOS_COUNT ||
        sample_rate == WIDEBAND_RATE || !audio_clock_rate(sample_rate)) {
        return false;
    }
    for (int index = 0; index < eq->section_count; index++) {
        if (!encode_section(&eq->sections[index], eq->section_count,
                            out->coeffs + index * SOS_BYTES)) {
            return false;
        }
    }
    out->control1 = SOS1_ENABLE;
    out->control2 = SOS1_CONTROL_PORT;
    if (eq->section_count >= 2) {
        out->control1 |= SOS2_ENABLE;
        out->control2 |= SOS2_CONTROL_PORT;
    }
    if (eq->section_count == 3) {
        out->control1 |= SOS3_ENABLE;
    }
    out->enabled = true;
    return true;
}

static bool write_pair(const DacBus *bus, uint32_t map, uint8_t value)
{
    bool left = bus->write(bus->context, DAC_LEFT_ADDRESS, map, value);
    return bus->write(bus->context, DAC_RIGHT_ADDRESS, map, value) && left;
}

bool dac_eq_disable(const DacBus *bus)
{
    bool disabled = write_pair(bus, FILTER_CONTROL_1, 0);
    return write_pair(bus, FILTER_CONTROL_2, 0) && disabled;
}

bool dac_eq_write(const DacBus *bus, const DacEqProgram *program)
{
    if (!program->enabled) {
        return dac_eq_disable(bus);
    }
    for (uint32_t index = 0; index < sizeof program->coeffs; index++) {
        if (!write_pair(bus, FILTER_COEFFICIENT_FIRST + index, program->coeffs[index])) {
            (void)dac_eq_disable(bus);
            return false;
        }
    }
    if (write_pair(bus, FILTER_CONTROL_2, program->control2) &&
        write_pair(bus, FILTER_CONTROL_1, program->control1)) {
        return true;
    }
    (void)dac_eq_disable(bus);
    return false;
}

bool dac_wideband_flatness_eligible(uint32_t sample_rate, uint8_t filter, bool hw_eq_enabled)
{
    return sample_rate == WIDEBAND_RATE && filter != NOS_FILTER_INDEX && !hw_eq_enabled;
}
