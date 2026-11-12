#include "device/dac_sequence.h"

#include <stddef.h>

enum {
    MICROSECONDS_PER_MILLISECOND = 1000,
    CHIP_COUNT = DAC_IDENTITY_CHIPS,
    STATUS_MAP = 0x0F0000,
    STATUS_NOT_READY = 0x03,
    STATUS_POWER_DOWN_DONE = 0x01,
    POWER_DOWN_LIMIT_MS = 150,
    POWER_CONTROL_MAP = 0x020000,
    SERIAL_PORT_BIT = 0x40,
    HEADPHONE_BIT = 0x10,
    OUTPUT_POWER_UP_MS = 12,
    RELEASE_WAIT_US = 100,
    MONO_GROUPS = 4,
    MONO_ITEMS = 6
};

#define WRITE(map, value) ((uint32_t)(map) << 8 | (value))

#define PCM_VOLUME_B 0x090001u
#define PCM_VOLUME_A 0x090002u
#define HEADPHONE_GAIN 0x080000u
#define PCM_FILTER 0x090000u
#define PCM_MUTE 0x090003u

enum {
    BALANCED_ATTENUATION_VALUE = 10,
    BLUETOOTH_TOP_VALUE = 10,
    VOLUME_UNITS_PER_STEP = 2,
    RAMP_STEP = 6,
    GAIN_HIGH_VALUE = 0x31,
    GAIN_LOW_VALUE = 0x20
};

static const uint8_t CHIPS[CHIP_COUNT] = {DAC_LEFT_ADDRESS, DAC_RIGHT_ADDRESS};
static const uint32_t IDENTITY_MAPS[DAC_IDENTITY_BYTES] = {0x010000, 0x010001, 0x010002, 0x010004,
                                                           0x010005};
static const uint8_t CURVE_TOP_VALUES[] = {
    [DAC_CURVE_SINGLE_ENDED] = 0,
    [DAC_CURVE_BALANCED] = BALANCED_ATTENUATION_VALUE,
    [DAC_CURVE_BLUETOOTH] = BLUETOOTH_TOP_VALUE,
};
static const uint8_t CHANNEL_VALUES[CHIP_COUNT] = {0x07, 0x0F};

static const uint32_t AFTER_STATUS[] = {
    WRITE(0x0F0010, 0xE7), WRITE(0x020000, 0xF6), WRITE(0x01000C, 0x04), WRITE(0x040010, 0x01),
    WRITE(0x040011, 0x00), WRITE(0x040012, 0x08), WRITE(0x040013, 0x00), WRITE(0x040014, 0x1F),
    WRITE(0x040015, 0x00), WRITE(0x040016, 0x3F), WRITE(0x040017, 0x00), WRITE(0x040018, 0x0C),
    WRITE(0x040019, 0x0A), WRITE(0x050000, 0x00), WRITE(0x050001, 0x00),
};

static const uint8_t FILTER_VALUES[DAC_ANALOG_FILTERS] = {0x02, 0x42, 0x82, 0xC2, 0x22};

static const uint32_t BEFORE_MONO[] = {
    WRITE(PCM_VOLUME_B, DAC_VOLUME_MUTED),
    WRITE(PCM_VOLUME_A, DAC_VOLUME_MUTED),
    WRITE(0x090003, 0xEC),
    WRITE(0x090004, 0x00),
};

static const uint8_t MONO_OFFSETS[MONO_ITEMS] = {0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x10};
static const uint8_t MONO_VALUES[MONO_GROUPS][MONO_ITEMS] = {
    {0x2F, 0xB4, 0x25, 0x80, 0x55, 0xFA},
    {0x2F, 0xB4, 0x25, 0x80, 0x4E, 0xFD},
    {0x2F, 0xB4, 0x25, 0x80, 0x7C, 0xFD},
    {0x2F, 0xB4, 0x25, 0x80, 0x52, 0xFE},
};

static const uint32_t AFTER_MONO[] = {
    WRITE(0x0D0000, 0x04), WRITE(0x0D0001, 0x00), WRITE(0x0F0010, 0x87),
    WRITE(0x0F0011, 0x07), WRITE(0x010006, 0x04), WRITE(0x01000D, 0x02),
    WRITE(0x010010, 0x99), WRITE(0x180000, 0x45), WRITE(0x010010, 0x00),
};

static void wait_ms(const DacBus *bus, uint32_t milliseconds)
{
    bus->delay_us(bus->context, milliseconds * MICROSECONDS_PER_MILLISECOND);
}

static void set_pin(const DacBus *bus, DacPin pin, bool level)
{
    bus->set_pin(bus->context, pin, level);
}

static bool write_both(const DacBus *bus, uint32_t map, uint8_t value)
{
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        if (!bus->write(bus->context, CHIPS[chip], map, value)) {
            return false;
        }
    }
    return true;
}

static bool write_all(const DacBus *bus, const uint32_t *writes, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (!write_both(bus, writes[index] >> 8, (uint8_t)writes[index])) {
            return false;
        }
    }
    return true;
}

static bool write_channel_values(const DacBus *bus)
{
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        uint8_t value = CHANNEL_VALUES[chip];
        if (!bus->write(bus->context, CHIPS[chip], 0x05000A, value) ||
            !bus->write(bus->context, CHIPS[chip], 0x05000B, value)) {
            return false;
        }
    }
    return true;
}

static bool write_mono_table(const DacBus *bus)
{
    for (uint32_t group = 0; group < MONO_GROUPS; group++) {
        for (int item = 0; item < MONO_ITEMS; item++) {
            uint32_t map = 0x180000u + group * 0x10u + MONO_OFFSETS[item];
            if (!write_both(bus, map, MONO_VALUES[group][item])) {
                return false;
            }
        }
    }
    return true;
}

static bool change_power_bit(const DacBus *bus, uint8_t bit, bool down)
{
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        uint8_t value = 0;
        bus->delay_us(bus->context, RELEASE_WAIT_US);
        if (!bus->read(bus->context, CHIPS[chip], POWER_CONTROL_MAP, &value) ||
            !bus->write(bus->context, CHIPS[chip], POWER_CONTROL_MAP,
                        (uint8_t)(down ? value | bit : value & ~bit))) {
            return false;
        }
    }
    return true;
}

static bool release_power_bit(const DacBus *bus, uint8_t bit)
{
    return change_power_bit(bus, bit, false);
}

static DacResult read_status(const DacBus *bus)
{
    int not_ready = 0;
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        uint8_t status = 0;
        if (!bus->read(bus->context, CHIPS[chip], STATUS_MAP, &status)) {
            return DAC_NO_REPLY;
        }
        not_ready += (status & STATUS_NOT_READY) == STATUS_NOT_READY ? 1 : 0;
    }
    return not_ready == CHIP_COUNT ? DAC_NOT_READY : DAC_READY;
}

void dac_power_pins(const DacBus *bus)
{
    set_pin(bus, DAC_PIN_MUTE, true);
    set_pin(bus, DAC_PIN_ENABLE_A, true);
    set_pin(bus, DAC_PIN_ENABLE_B, true);
    wait_ms(bus, 2);
    set_pin(bus, DAC_PIN_RESET, false);
    wait_ms(bus, 1);
    set_pin(bus, DAC_PIN_RESET, true);
    wait_ms(bus, 8);
    set_pin(bus, DAC_PIN_RESET, false);
    wait_ms(bus, 2);
}

static void read_chip_identity(const DacBus *bus, uint8_t address, DacIdentity *chip)
{
    *chip = (DacIdentity){.source = DAC_IDENTITY_READ};
    for (int index = 0; index < DAC_IDENTITY_BYTES; index++) {
        if (!bus->read(bus->context, address, IDENTITY_MAPS[index], &chip->bytes[index])) {
            chip->bytes[index] = 0;
            return;
        }
        chip->read_mask |= (uint8_t)(1u << index);
    }
}

void dac_read_identity(const DacBus *bus, DacIdentity *chips)
{
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        read_chip_identity(bus, CHIPS[chip], &chips[chip]);
    }
}

bool dac_probe(const DacBus *bus)
{
    return write_both(bus, 0x000000, 0x00);
}

static bool start_up(const DacBus *bus)
{
    if (!write_both(bus, 0x0B0000, 0x1F)) {
        return false;
    }
    wait_ms(bus, 80);
    set_pin(bus, DAC_PIN_AMP, true);
    return write_both(bus, 0x020052, 0x02);
}

static uint8_t filter_value(uint8_t filter)
{
    return FILTER_VALUES[filter < DAC_ANALOG_FILTERS ? filter : 0];
}

static uint8_t gain_value(bool high)
{
    return high ? GAIN_HIGH_VALUE : GAIN_LOW_VALUE;
}

static bool set_up_blocks(const DacBus *bus, DacAnalog analog)
{
    if (!write_all(bus, AFTER_STATUS, sizeof AFTER_STATUS / sizeof AFTER_STATUS[0]) ||
        !write_channel_values(bus) || !write_both(bus, PCM_FILTER, filter_value(analog.filter)) ||
        !write_all(bus, BEFORE_MONO, sizeof BEFORE_MONO / sizeof BEFORE_MONO[0]) ||
        !write_mono_table(bus) || !write_both(bus, 0x0B0000, 0x1F)) {
        return false;
    }
    wait_ms(bus, 80);
    return write_both(bus, HEADPHONE_GAIN, gain_value(analog.gain_high)) &&
           write_all(bus, AFTER_MONO, sizeof AFTER_MONO / sizeof AFTER_MONO[0]);
}

bool dac_sequence_power_up(const DacBus *bus)
{
    if (!write_both(bus, 0x010010, 0x99) || !write_both(bus, 0x080032, 0x20) ||
        !release_power_bit(bus, SERIAL_PORT_BIT) || !release_power_bit(bus, HEADPHONE_BIT)) {
        return false;
    }
    wait_ms(bus, OUTPUT_POWER_UP_MS);
    return write_both(bus, 0x080032, 0x00) && write_both(bus, 0x010010, 0x00);
}

static bool release_outputs(const DacBus *bus, uint8_t rate_byte)
{
    return dac_sequence_power_up(bus) && write_both(bus, 0x01000B, rate_byte);
}

DacResult dac_configure(const DacBus *bus, uint8_t rate_byte, DacAnalog analog)
{
    if (!start_up(bus)) {
        return DAC_NO_REPLY;
    }
    DacResult status = read_status(bus);
    if (status != DAC_READY) {
        return status;
    }
    return set_up_blocks(bus, analog) && release_outputs(bus, rate_byte) ? DAC_READY : DAC_NO_REPLY;
}

void dac_set_volume(const DacBus *bus, uint8_t volume)
{
    write_both(bus, PCM_VOLUME_B, volume);
    write_both(bus, PCM_VOLUME_A, volume);
}

static bool change_analog(const DacBus *bus, DacAnalog after)
{
    return dac_sequence_power_down(bus) &&
           write_both(bus, HEADPHONE_GAIN, gain_value(after.gain_high)) &&
           write_both(bus, PCM_FILTER, filter_value(after.filter)) && dac_sequence_power_up(bus) &&
           write_both(bus, 0x0F0010, 0x87) && write_both(bus, PCM_MUTE, 0xEC);
}

bool dac_sequence_gain(const DacBus *bus, bool high)
{
    return dac_sequence_power_down(bus) && write_both(bus, HEADPHONE_GAIN, gain_value(high)) &&
           dac_sequence_power_up(bus) && write_both(bus, 0x0F0010, 0x87) &&
           write_both(bus, PCM_MUTE, 0xEC);
}

static bool change_filter(const DacBus *bus, uint8_t filter)
{
    if (!write_both(bus, PCM_MUTE, 0xEF)) {
        return false;
    }
    wait_ms(bus, 150);
    bool written = write_both(bus, PCM_FILTER, filter_value(filter));
    return write_both(bus, PCM_MUTE, 0xEC) && written;
}

bool dac_apply_analog(const DacBus *bus, DacAnalog before, DacAnalog after)
{
    if (after.filter >= DAC_ANALOG_FILTERS) {
        return false;
    }
    if (before.gain_high != after.gain_high) {
        return change_analog(bus, after);
    }
    return before.filter == after.filter || change_filter(bus, after.filter);
}

uint8_t dac_volume_value(uint8_t level, DacCurve curve)
{
    if (level == 0) {
        return DAC_VOLUME_MUTED;
    }
    uint32_t capped = level < DAC_VOLUME_LEVELS ? level : DAC_VOLUME_LEVELS;
    return (uint8_t)(CURVE_TOP_VALUES[curve] +
                     VOLUME_UNITS_PER_STEP * (DAC_VOLUME_LEVELS - capped));
}

void dac_output_on(const DacBus *bus, uint8_t volume)
{
    set_pin(bus, DAC_PIN_ENABLE_A, true);
    set_pin(bus, DAC_PIN_MUTE, true);
    set_pin(bus, DAC_PIN_AMP, true);
    write_channel_values(bus);
    dac_set_volume(bus, volume);
    wait_ms(bus, 10);
    set_pin(bus, DAC_PIN_MUTE, false);
}

static bool wait_for_power_down(const DacBus *bus)
{
    uint32_t waited_ms = 0;
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        uint8_t status = 0;
        do {
            if (!bus->read(bus->context, CHIPS[chip], STATUS_MAP, &status)) {
                return false;
            }
            if ((status & STATUS_POWER_DOWN_DONE) != 0) {
                break;
            }
            if (waited_ms == POWER_DOWN_LIMIT_MS) {
                return false;
            }
            wait_ms(bus, 1);
            waited_ms++;
        } while (true);
    }
    return true;
}

bool dac_sequence_power_down(const DacBus *bus)
{
    if (!write_both(bus, 0x010010, 0x99) || !write_both(bus, 0x080046, 0x21) ||
        !write_both(bus, 0x090003, 0xEF)) {
        return false;
    }
    wait_ms(bus, 150);
    return read_status(bus) != DAC_NO_REPLY && write_both(bus, 0x0F0010, 0x86) &&
           change_power_bit(bus, HEADPHONE_BIT, true) && wait_for_power_down(bus) &&
           change_power_bit(bus, SERIAL_PORT_BIT, true) && write_both(bus, 0x080046, 0x20);
}

void dac_output_off(const DacBus *bus)
{
    set_pin(bus, DAC_PIN_MUTE, true);
    set_pin(bus, DAC_PIN_ENABLE_A, true);
    dac_set_volume(bus, DAC_VOLUME_MUTED);
    (void)dac_sequence_power_down(bus);
    write_both(bus, 0x0B0000, 0x00);
    wait_ms(bus, 10);
    set_pin(bus, DAC_PIN_AMP, false);
    set_pin(bus, DAC_PIN_RESET, true);
    set_pin(bus, DAC_PIN_ENABLE_B, false);
}

void dac_set_muted(const DacBus *bus, bool muted, uint8_t volume)
{
    if (muted) {
        set_pin(bus, DAC_PIN_MUTE, true);
        dac_set_volume(bus, DAC_VOLUME_MUTED);
        return;
    }
    dac_set_volume(bus, volume);
    set_pin(bus, DAC_PIN_MUTE, false);
}

uint8_t dac_ramp_next(uint8_t current, uint8_t target)
{
    if (current < target) {
        return target - current > RAMP_STEP ? (uint8_t)(current + RAMP_STEP) : target;
    }
    return current - target > RAMP_STEP ? (uint8_t)(current - RAMP_STEP) : target;
}
