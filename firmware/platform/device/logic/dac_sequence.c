#include "device/dac_sequence.h"

#include "core/seam.h"
#include "device/dac_eq.h"
#include <stddef.h>

enum {
    MICROSECONDS_PER_MILLISECOND = 1000,
    CHIP_COUNT = DAC_IDENTITY_CHIPS,
    STATUS_MAP = 0x0F0000,
    STATUS_NOT_READY = 0x03,
    STATUS_POWER_DOWN_DONE = 0x01,
    POWER_DOWN_POLL_MS = 5,
    POWER_DOWN_LIMIT_MS = 200,
    POWER_CONTROL_MAP = 0x020000,
    SYSTEM_CLOCK_MAP = 0x010006,
    MCLK_44K_FAMILY = 0x04,
    RATE_192K = 6,
    PCM_WIDEBAND = 0x04,
    PCM_NOS_FILTER = 4,
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
#define DSD_PATH 0x070004u
#define DSD_SIGCTL 0x070006u
#define DSD_VOLUME_A 0x070001u
#define SERIAL_RATE 0x01000Bu

enum {
    BALANCED_ATTENUATION_VALUE = 10,
    BLUETOOTH_TOP_VALUE = 10,
    VOLUME_UNITS_PER_STEP = 2,
    RAMP_STEP = 6
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

static const uint32_t DOP_SETUP[] = {
    WRITE(0x01000C, 0x05),   WRITE(0x040012, 0x02), WRITE(DSD_VOLUME_A, DAC_VOLUME_MUTED),
    WRITE(DSD_SIGCTL, 0xD0), WRITE(DSD_PATH, 0x52),
};

static const uint32_t DOP_CLEAR[] = {
    WRITE(DSD_PATH, 0x02),
    WRITE(DSD_SIGCTL, 0x00),
};

static const uint8_t MONO_OFFSETS[MONO_ITEMS] = {0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x10};
static const uint8_t MONO_VALUES[MONO_GROUPS][MONO_ITEMS] = {
    {0x2F, 0xB4, 0x25, 0x80, 0x55, 0xFA},
    {0x2F, 0xB4, 0x25, 0x80, 0x4E, 0xFD},
    {0x2F, 0xB4, 0x25, 0x80, 0x7C, 0xFD},
    {0x2F, 0xB4, 0x25, 0x80, 0x52, 0xFE},
};

static const uint32_t AFTER_MONO[] = {
    WRITE(0x0D0000, 0x04),
    WRITE(0x0D0001, 0x00),
    WRITE(0x0F0010, 0x87),
    WRITE(0x0F0011, 0x07),
};

static const uint32_t AFTER_CLOCK[] = {
    WRITE(0x01000D, 0x02),
    WRITE(0x010010, 0x99),
    WRITE(0x180000, 0x45),
    WRITE(0x010010, 0x00),
};

static const uint32_t POWER_UP_PRE[] = {
    WRITE(0x010010, 0x99),
    WRITE(0x080032, 0x20),
};

static const uint32_t POWER_UP_POST[] = {
    WRITE(0x080032, 0x00),
    WRITE(0x010010, 0x00),
};

static const uint32_t POWER_DOWN_PRE[] = {
    WRITE(0x010010, 0x99),
    WRITE(0x080046, 0x21),
    WRITE(0x010010, 0x00),
    WRITE(0x090003, 0xEF),
};

static const uint32_t POWER_DOWN_POST[] = {
    WRITE(0x010010, 0x99),
    WRITE(0x080046, 0x20),
    WRITE(0x010010, 0x00),
};

static const uint32_t ANALOG_POST[] = {
    WRITE(0x0F0010, 0x87),
    WRITE(PCM_MUTE, 0xEC),
};

typedef struct PinStep {
    uint8_t pin;
    uint8_t level;
    uint8_t delay_ms;
} PinStep;

static const PinStep POWER_PIN_STEPS[] = {
    {DAC_PIN_MUTE, 1, 0},  {DAC_PIN_ENABLE_A, 1, 0}, {DAC_PIN_ENABLE_B, 1, 2},
    {DAC_PIN_RESET, 0, 1}, {DAC_PIN_RESET, 1, 8},    {DAC_PIN_RESET, 0, 2},
};

static const PinStep OUTPUT_ON_PINS[] = {
    {DAC_PIN_ENABLE_A, 1, 0},
    {DAC_PIN_MUTE, 1, 0},
    {DAC_PIN_AMP, 1, 0},
};

static const PinStep OUTPUT_OFF_PRE_PINS[] = {
    {DAC_PIN_MUTE, 1, 0},
    {DAC_PIN_ENABLE_A, 1, 0},
};

static const PinStep OUTPUT_OFF_POST_PINS[] = {
    {DAC_PIN_AMP, 0, 0},
    {DAC_PIN_RESET, 1, 0},
    {DAC_PIN_ENABLE_B, 0, 0},
};

static void wait_ms(const DacBus *bus, uint32_t milliseconds)
{
    bus->delay_us(bus->context, milliseconds * MICROSECONDS_PER_MILLISECOND);
}

static void set_pin(const DacBus *bus, DacPin pin, bool level)
{
    bus->set_pin(bus->context, pin, level);
}

CORE_COLD static void run_pin_steps(const DacBus *bus, const PinStep *steps, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        set_pin(bus, (DacPin)steps[index].pin, steps[index].level != 0);
        if (steps[index].delay_ms != 0) {
            wait_ms(bus, steps[index].delay_ms);
        }
    }
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

static bool dop_mode(const DacBus *bus)
{
    return bus->dop != NULL && *bus->dop;
}

CORE_COLD static bool write_all(const DacBus *bus, const uint32_t *writes, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (!write_both(bus, writes[index] >> 8, (uint8_t)writes[index])) {
            return false;
        }
    }
    return true;
}

CORE_COLD static bool write_channel_values(const DacBus *bus)
{
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        uint8_t value = CHANNEL_VALUES[chip] - dop_mode(bus);
        if (!bus->write(bus->context, CHIPS[chip], 0x05000A, value) ||
            !bus->write(bus->context, CHIPS[chip], 0x05000B, value)) {
            return false;
        }
    }
    return true;
}

CORE_COLD static bool write_mono_table(const DacBus *bus)
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

CORE_COLD static bool change_power_bit_checked(const DacBus *bus, uint8_t bit, bool down,
                                               bool *changed)
{
    bool any_changed = false;
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        uint8_t value = 0;
        bus->delay_us(bus->context, RELEASE_WAIT_US);
        if (!bus->read(bus->context, CHIPS[chip], POWER_CONTROL_MAP, &value)) {
            return false;
        }
        uint8_t next = (uint8_t)(down ? value | bit : value & ~bit);
        any_changed |= next != value;
        if (!bus->write(bus->context, CHIPS[chip], POWER_CONTROL_MAP, next)) {
            return false;
        }
    }
    if (changed != NULL) {
        *changed = any_changed;
    }
    return true;
}

CORE_COLD static bool change_power_bit(const DacBus *bus, uint8_t bit, bool down)
{
    return change_power_bit_checked(bus, bit, down, NULL);
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

CORE_COLD void dac_power_pins(const DacBus *bus)
{
    run_pin_steps(bus, POWER_PIN_STEPS, sizeof POWER_PIN_STEPS / sizeof POWER_PIN_STEPS[0]);
}

CORE_COLD static void read_chip_identity(const DacBus *bus, uint8_t address, DacIdentity *chip)
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

CORE_COLD void dac_read_identity(const DacBus *bus, DacIdentity *chips)
{
    for (int chip = 0; chip < CHIP_COUNT; chip++) {
        read_chip_identity(bus, CHIPS[chip], &chips[chip]);
    }
}

CORE_COLD bool dac_probe(const DacBus *bus)
{
    return write_both(bus, 0x000000, 0x00);
}

CORE_COLD static bool start_up(const DacBus *bus)
{
    if (!write_both(bus, 0x0B0000, 0x1F)) {
        return false;
    }
    wait_ms(bus, 80);
    set_pin(bus, DAC_PIN_AMP, true);
    return write_both(bus, 0x020052, 0x02);
}

static uint8_t filter_value(uint8_t filter, bool wideband)
{
    return FILTER_VALUES[filter < DAC_ANALOG_FILTERS ? filter : 0] |
           (wideband && filter != PCM_NOS_FILTER ? PCM_WIDEBAND : 0);
}

static uint8_t gain_value(bool high)
{
    return high ? DAC_GAIN_HIGH_VALUE : DAC_GAIN_LOW_VALUE;
}

CORE_COLD static bool set_up_blocks(const DacBus *bus, DacAnalog analog, uint8_t rate_byte)
{
    bool wideband = rate_byte == RATE_192K && !dop_mode(bus);
    if (!write_all(bus, AFTER_STATUS, sizeof AFTER_STATUS / sizeof AFTER_STATUS[0]) ||
        !write_channel_values(bus) ||
        !write_both(bus, PCM_FILTER, filter_value(analog.filter, wideband)) ||
        !write_all(bus, BEFORE_MONO, sizeof BEFORE_MONO / sizeof BEFORE_MONO[0]) ||
        !write_mono_table(bus) || !write_both(bus, 0x0B0000, 0x1F)) {
        return false;
    }
    wait_ms(bus, 80);
    return write_both(bus, HEADPHONE_GAIN, gain_value(analog.gain_high)) &&
           write_all(bus, AFTER_MONO, sizeof AFTER_MONO / sizeof AFTER_MONO[0]) &&
           write_both(bus, SYSTEM_CLOCK_MAP, rate_byte & 1u ? MCLK_44K_FAMILY : 0) &&
           write_all(bus, AFTER_CLOCK, sizeof AFTER_CLOCK / sizeof AFTER_CLOCK[0]);
}

CORE_COLD bool dac_sequence_power_up(const DacBus *bus)
{
    if (!write_all(bus, POWER_UP_PRE, sizeof POWER_UP_PRE / sizeof POWER_UP_PRE[0]) ||
        !release_power_bit(bus, SERIAL_PORT_BIT) || !release_power_bit(bus, HEADPHONE_BIT)) {
        return false;
    }
    wait_ms(bus, OUTPUT_POWER_UP_MS);
    return write_all(bus, POWER_UP_POST, sizeof POWER_UP_POST / sizeof POWER_UP_POST[0]);
}

CORE_COLD static bool release_outputs(const DacBus *bus, uint8_t rate_byte)
{
    bool dop = dop_mode(bus);
    return write_both(bus, SERIAL_RATE, rate_byte) &&
           write_all(bus, dop ? DOP_SETUP : DOP_CLEAR,
                     dop ? sizeof DOP_SETUP / sizeof DOP_SETUP[0]
                         : sizeof DOP_CLEAR / sizeof DOP_CLEAR[0]) &&
           dac_sequence_power_up(bus);
}

CORE_COLD DacResult dac_configure(const DacBus *bus, uint8_t rate_byte, DacAnalog analog)
{
    if (!start_up(bus)) {
        return DAC_NO_REPLY;
    }
    DacResult status = read_status(bus);
    if (status != DAC_READY) {
        return status;
    }
    return set_up_blocks(bus, analog, rate_byte) && release_outputs(bus, rate_byte) ? DAC_READY
                                                                                    : DAC_NO_REPLY;
}

CORE_COLD bool dac_set_volume_lr(const DacBus *bus, uint8_t left, uint8_t right)
{
    uint32_t map = dop_mode(bus) ? DSD_VOLUME_A : PCM_VOLUME_A;
    return bus->write(bus->context, DAC_LEFT_ADDRESS, map, left) &&
           bus->write(bus->context, DAC_RIGHT_ADDRESS, map, right);
}

CORE_SEAM bool dac_set_volume(const DacBus *bus, uint8_t volume)
{
    return dac_set_volume_lr(bus, volume, volume);
}

CORE_COLD static bool set_volume_with_retry(const DacBus *bus, uint8_t volume)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (dac_set_volume(bus, volume)) {
            return true;
        }
    }
    return false;
}

CORE_COLD static bool write_channel_values_with_retry(const DacBus *bus)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (write_channel_values(bus)) {
            return true;
        }
    }
    return false;
}

CORE_COLD static bool restore_analog(const DacBus *bus)
{
    return dac_sequence_power_up(bus) &&
           write_all(bus, ANALOG_POST, sizeof ANALOG_POST / sizeof ANALOG_POST[0]);
}

CORE_COLD DacEqResult dac_sequence_equalizer(const DacBus *bus, const DacEqProgram *program,
                                             uint8_t filter, bool wideband_rate)
{
    if (!dac_sequence_power_down(bus)) {
        return DAC_EQ_FAILED;
    }
    bool enabled = program != NULL && program->enabled &&
                   (!wideband_rate || write_both(bus, PCM_FILTER, filter_value(filter, false))) &&
                   dac_eq_write(bus, program);
    if ((!enabled &&
         (!dac_eq_disable(bus) ||
          (wideband_rate && !write_both(bus, PCM_FILTER, filter_value(filter, true))))) ||
        !restore_analog(bus)) {
        return DAC_EQ_FAILED;
    }
    return enabled ? DAC_EQ_ON : DAC_EQ_OFF;
}

CORE_COLD static bool change_analog(const DacBus *bus, DacAnalog after, uint8_t filter)
{
    return dac_sequence_power_down(bus) &&
           write_both(bus, HEADPHONE_GAIN, gain_value(after.gain_high)) &&
           write_both(bus, PCM_FILTER, filter) && restore_analog(bus);
}

CORE_COLD static bool change_filter(const DacBus *bus, uint8_t filter)
{
    if (!write_both(bus, PCM_MUTE, 0xEF)) {
        return false;
    }
    wait_ms(bus, 150);
    return write_both(bus, PCM_FILTER, filter) && write_both(bus, PCM_MUTE, 0xEC);
}

CORE_SEAM CORE_COLD bool dac_apply_analog(const DacBus *bus, DacAnalog before, DacAnalog after)
{
    if (after.filter >= DAC_ANALOG_FILTERS) {
        return false;
    }
    bool wideband = bus->wideband != NULL && *bus->wideband;
    uint8_t filter = filter_value(after.filter, wideband);
    bool nos_changed = (before.filter == PCM_NOS_FILTER) != (after.filter == PCM_NOS_FILTER);
    bool power_down = before.gain_high != after.gain_high || before.filter == DAC_ANALOG_UNKNOWN ||
                      (wideband && nos_changed);
    bool applied = power_down ? change_analog(bus, after, filter)
                              : before.filter == after.filter || change_filter(bus, filter);
    if (!applied) {
        set_pin(bus, DAC_PIN_MUTE, true);
    }
    return applied;
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

CORE_SEAM void dac_output_on(const DacBus *bus, uint8_t volume)
{
    run_pin_steps(bus, OUTPUT_ON_PINS, sizeof OUTPUT_ON_PINS / sizeof OUTPUT_ON_PINS[0]);
    if ((!dop_mode(bus) && !write_channel_values_with_retry(bus)) ||
        !set_volume_with_retry(bus, volume)) {
        return;
    }
    wait_ms(bus, 10);
    set_pin(bus, DAC_PIN_MUTE, false);
}

CORE_COLD static bool wait_for_power_down(const DacBus *bus)
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
            if (waited_ms >= POWER_DOWN_LIMIT_MS) {
                return true;
            }
            wait_ms(bus, POWER_DOWN_POLL_MS);
            waited_ms += POWER_DOWN_POLL_MS;
        } while (true);
    }
    return true;
}

CORE_COLD bool dac_sequence_power_down(const DacBus *bus)
{
    if (!write_all(bus, POWER_DOWN_PRE, sizeof POWER_DOWN_PRE / sizeof POWER_DOWN_PRE[0])) {
        return false;
    }
    wait_ms(bus, 150);
    bool powered_down = false;
    return read_status(bus) != DAC_NO_REPLY && write_both(bus, 0x0F0010, 0x86) &&
           change_power_bit_checked(bus, HEADPHONE_BIT, true, &powered_down) &&
           (!powered_down || wait_for_power_down(bus)) &&
           change_power_bit(bus, SERIAL_PORT_BIT, true) &&
           write_all(bus, POWER_DOWN_POST, sizeof POWER_DOWN_POST / sizeof POWER_DOWN_POST[0]);
}

CORE_COLD void dac_output_off(const DacBus *bus)
{
    run_pin_steps(bus, OUTPUT_OFF_PRE_PINS,
                  sizeof OUTPUT_OFF_PRE_PINS / sizeof OUTPUT_OFF_PRE_PINS[0]);
    dac_set_volume(bus, DAC_VOLUME_MUTED);
    if (dac_sequence_power_down(bus) && dop_mode(bus)) {
        write_all(bus, DOP_CLEAR, sizeof DOP_CLEAR / sizeof DOP_CLEAR[0]);
    }
    write_both(bus, 0x0B0000, 0x00);
    wait_ms(bus, 10);
    run_pin_steps(bus, OUTPUT_OFF_POST_PINS,
                  sizeof OUTPUT_OFF_POST_PINS / sizeof OUTPUT_OFF_POST_PINS[0]);
}

CORE_SEAM void dac_set_muted(const DacBus *bus, bool muted, uint8_t volume)
{
    if (muted) {
        set_pin(bus, DAC_PIN_MUTE, true);
        set_volume_with_retry(bus, DAC_VOLUME_MUTED);
        return;
    }
    set_pin(bus, DAC_PIN_MUTE, !set_volume_with_retry(bus, volume));
}

uint8_t dac_ramp_next(uint8_t current, uint8_t target)
{
    if (current < target) {
        return target - current > RAMP_STEP ? (uint8_t)(current + RAMP_STEP) : target;
    }
    return current - target > RAMP_STEP ? (uint8_t)(current - RAMP_STEP) : target;
}

bool dac_fade_volume(const DacBus *bus, DacLevel *level, uint8_t target, uint32_t step_us)
{
    DacStereoLevel stereo = {level->applied, level->applied, level->stale};
    bool ok = dac_fade_volume_lr(bus, &stereo, (DacStereoLevel){target, target, false}, step_us);
    level->applied = stereo.left;
    level->stale = stereo.stale;
    return ok;
}

static const uint8_t BALANCE_LOW[] = {255, 80, 68, 61, 56, 52, 49, 46, 44, 42, 40};
static const uint8_t BALANCE_TENS[] = {40, 28, 21, 16, 12, 9, 6, 4, 2, 0};

CORE_COLD uint8_t dac_balance_attenuation(uint32_t gain, uint32_t trim)
{
    if (trim == 0 || gain >= trim) {
        return 0;
    }
    uint32_t percent = (gain * 100 + trim / 2) / trim;
    if (percent <= 10) {
        return BALANCE_LOW[percent];
    }
    if (percent >= 100) {
        return 0;
    }
    uint32_t ten = percent / 10 - 1;
    uint32_t rem = percent % 10;
    return (uint8_t)((BALANCE_TENS[ten] * (10 - rem) + BALANCE_TENS[ten + 1] * rem + 5) / 10);
}

CORE_COLD uint8_t dac_balance_volume(uint8_t volume, uint8_t attenuation)
{
    if (volume == DAC_VOLUME_MUTED || attenuation == DAC_VOLUME_MUTED) {
        return DAC_VOLUME_MUTED;
    }
    unsigned sum = volume + attenuation;
    return sum < DAC_VOLUME_MUTED ? (uint8_t)sum : DAC_VOLUME_MUTED - 1;
}

CORE_COLD static bool write_stereo_volume(const DacBus *bus, uint8_t left, uint8_t right)
{
    return left == right ? dac_set_volume(bus, left) : dac_set_volume_lr(bus, left, right);
}

CORE_COLD bool dac_fade_volume_lr(const DacBus *bus, DacStereoLevel *level, DacStereoLevel target,
                                  uint32_t step_us)
{
    if (level->stale && !write_stereo_volume(bus, level->left, level->right)) {
        return false;
    }
    level->stale = false;
    while (level->left != target.left || level->right != target.right) {
        uint8_t left = dac_ramp_next(level->left, target.left);
        uint8_t right = dac_ramp_next(level->right, target.right);
        if (!write_stereo_volume(bus, left, right)) {
            level->stale = true;
            return false;
        }
        level->left = left;
        level->right = right;
        bus->delay_us(bus->context, step_us);
    }
    return true;
}
