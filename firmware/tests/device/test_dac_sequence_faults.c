#include <stdint.h>

#include "check.h"
#include "device/dac_sequence.h"

enum {
    POWER = 0x020000,
    HEADPHONE_DOWN = 0x10,
    SERIAL_DOWN = 0x40,
    STATUS = 0x0F0000,
    GAIN = 0x080000,
    FILTER = 0x090000,
    VOLUME = 0x090002,
    PCM_MUTE = 0x090003,
    SAMPLE_RATE = 0x01000B,
    CHANNEL_A = 0x05000A,
    CHANNEL_B = 0x05000B,
    OUTPUT = 0x0B0000,
    TARGET_VOLUME = 26
};

static const uint32_t MAPS[] = {POWER,       GAIN,   FILTER,    VOLUME,   PCM_MUTE,
                                SAMPLE_RATE, OUTPUT, CHANNEL_A, CHANNEL_B};
static const DacAnalog BEFORE = {.filter = 0};
static const DacAnalog AFTER = {.gain_high = true, .filter = 3};
static const DacAnalog FILTER_ONLY = {.filter = 3};

typedef struct FakeDac {
    uint8_t registers[2][TEST_CASE_COUNT(MAPS)];
    bool pins[DAC_PIN_AMP + 1];
    DacLevel level;
    uint32_t transactions;
    uint32_t fail_at;
    bool failed;
    uint32_t pcm_unmutes;
} FakeDac;

static uint8_t *register_at(FakeDac *dac, uint8_t address, uint32_t map)
{
    CHECK(address == DAC_LEFT_ADDRESS || address == DAC_RIGHT_ADDRESS);
    for (size_t index = 0; index < TEST_CASE_COUNT(MAPS); index++) {
        if (MAPS[index] == map) {
            return &dac->registers[address == DAC_RIGHT_ADDRESS][index];
        }
    }
    return NULL;
}

static bool nak(FakeDac *dac)
{
    bool failed = dac->transactions++ == dac->fail_at;
    dac->failed |= failed;
    return failed;
}

static bool write_register(void *context, uint8_t address, uint32_t map, uint8_t value)
{
    FakeDac *dac = context;
    if (nak(dac)) {
        return false;
    }
    dac->pcm_unmutes += map == PCM_MUTE && value == 0xEC;
    uint8_t *reg = register_at(dac, address, map);
    if (reg != NULL) {
        *reg = value;
    }
    return true;
}

static bool read_register(void *context, uint8_t address, uint32_t map, uint8_t *value)
{
    FakeDac *dac = context;
    if (nak(dac)) {
        return false;
    }
    if (map == STATUS) {
        *value = (*register_at(dac, address, POWER) & HEADPHONE_DOWN) != 0;
        return true;
    }
    CHECK_EQUAL(POWER, map);
    *value = *register_at(dac, address, map);
    return true;
}

static void set_pin(void *context, DacPin pin, bool level)
{
    FakeDac *dac = context;
    dac->pins[pin] = level;
}

static void delay(void *context, uint32_t us)
{
    (void)context;
    (void)us;
}

static DacBus bus_for(FakeDac *dac)
{
    return (DacBus){write_register, read_register, set_pin, delay, dac, NULL};
}

static void both(FakeDac *dac, uint32_t map, uint8_t value)
{
    CHECK_EQUAL(value, *register_at(dac, DAC_LEFT_ADDRESS, map));
    CHECK_EQUAL(value, *register_at(dac, DAC_RIGHT_ADDRESS, map));
}

typedef enum Operation {
    CONFIGURE,
    START_OUTPUT,
    GAIN_APPLY,
    FILTER_APPLY,
    MUTE,
    UNMUTE,
    FADE,
    POWER_DOWN,
    STOP
} Operation;

static FakeDac initial_state(Operation op)
{
    FakeDac dac = {.fail_at = UINT32_MAX, .level = {.applied = 80}};
    DacBus bus = bus_for(&dac);
    dac_power_pins(&bus);
    if (op != CONFIGURE) {
        CHECK_EQUAL(DAC_READY, dac_configure(&bus, 1, BEFORE));
        dac_output_on(&bus, dac.level.applied);
    }
    if (op == START_OUTPUT || op == UNMUTE || op == GAIN_APPLY || op == FILTER_APPLY) {
        dac_set_muted(&bus, true, DAC_VOLUME_MUTED);
    }
    if (op == START_OUTPUT) {
        *register_at(&dac, DAC_LEFT_ADDRESS, CHANNEL_A) = 0;
        *register_at(&dac, DAC_RIGHT_ADDRESS, CHANNEL_B) = 0;
    }
    dac.transactions = 0;
    dac.pcm_unmutes = 0;
    return dac;
}

static bool run(FakeDac *dac, Operation op)
{
    DacBus bus = bus_for(dac);
    switch (op) {
    case CONFIGURE:
        return dac_configure(&bus, 2, AFTER) == DAC_READY;
    case START_OUTPUT:
        dac_output_on(&bus, TARGET_VOLUME);
        return true;
    case GAIN_APPLY:
        return dac_apply_analog(&bus, BEFORE, AFTER);
    case FILTER_APPLY:
        return dac_apply_analog(&bus, BEFORE, FILTER_ONLY);
    case MUTE:
    case UNMUTE:
        dac_set_muted(&bus, op == MUTE, TARGET_VOLUME);
        return true;
    case FADE:
        return dac_fade_volume(&bus, &dac->level, TARGET_VOLUME, 300);
    case POWER_DOWN:
        return dac_sequence_power_down(&bus);
    case STOP:
        dac_output_off(&bus);
        return true;
    }
    return false;
}

static void check_completed(FakeDac *dac, Operation op)
{
    switch (op) {
    case CONFIGURE:
        both(dac, SAMPLE_RATE, 2);
        both(dac, VOLUME, DAC_VOLUME_MUTED);
        both(dac, GAIN, DAC_GAIN_HIGH_VALUE);
        both(dac, FILTER, 0xC2);
        break;
    case GAIN_APPLY:
    case FILTER_APPLY:
        both(dac, GAIN, op == GAIN_APPLY ? DAC_GAIN_HIGH_VALUE : DAC_GAIN_LOW_VALUE);
        both(dac, FILTER, 0xC2);
        both(dac, POWER, 0xA6);
        both(dac, PCM_MUTE, 0xEC);
        CHECK(dac->pins[DAC_PIN_MUTE]);
        break;
    case START_OUTPUT:
    case UNMUTE:
        both(dac, VOLUME, TARGET_VOLUME);
        CHECK(!dac->pins[DAC_PIN_MUTE]);
        if (op == START_OUTPUT) {
            CHECK_EQUAL(0x07, *register_at(dac, DAC_LEFT_ADDRESS, CHANNEL_A));
            CHECK_EQUAL(0x0F, *register_at(dac, DAC_RIGHT_ADDRESS, CHANNEL_B));
        }
        break;
    case MUTE:
    case STOP:
        both(dac, VOLUME, DAC_VOLUME_MUTED);
        CHECK(dac->pins[DAC_PIN_MUTE]);
        if (op == STOP) {
            both(dac, OUTPUT, 0);
            CHECK(!dac->pins[DAC_PIN_ENABLE_B]);
            CHECK(!dac->pins[DAC_PIN_AMP]);
            CHECK(dac->pins[DAC_PIN_RESET]);
        }
        break;
    case POWER_DOWN:
        both(dac, POWER, 0xF6);
        both(dac, PCM_MUTE, 0xEF);
        break;
    case FADE:
        both(dac, VOLUME, TARGET_VOLUME);
        CHECK_EQUAL(TARGET_VOLUME, dac->level.applied);
        CHECK(!dac->level.stale);
        break;
    }
}

static void check_fault(FakeDac *dac, Operation op, bool ok)
{
    if (op == CONFIGURE || op == GAIN_APPLY || op == FILTER_APPLY || op == FADE ||
        op == POWER_DOWN) {
        CHECK(!ok);
    }
    if (op == CONFIGURE || op == GAIN_APPLY || op == FILTER_APPLY || op == MUTE || op == STOP) {
        CHECK(dac->pins[DAC_PIN_MUTE]);
    }
    if ((op == START_OUTPUT || op == UNMUTE) && !dac->pins[DAC_PIN_MUTE]) {
        check_completed(dac, op);
    }
    if (op == FILTER_APPLY && (*register_at(dac, DAC_LEFT_ADDRESS, FILTER) != 0xC2 ||
                               *register_at(dac, DAC_RIGHT_ADDRESS, FILTER) != 0xC2)) {
        CHECK_EQUAL(0, dac->pcm_unmutes);
    }
}

static void sweep(Operation op)
{
    const FakeDac initial = initial_state(op);
    FakeDac baseline = initial;
    CHECK(run(&baseline, op));
    check_completed(&baseline, op);
    CHECK(baseline.transactions > 0);
    for (uint32_t index = 0; index <= baseline.transactions; index++) {
        FakeDac dac = initial;
        dac.fail_at = index;
        bool ok = run(&dac, op);
        CHECK_EQUAL(index < baseline.transactions, dac.failed);
        if (dac.failed) {
            check_fault(&dac, op, ok);
            dac.fail_at = UINT32_MAX;
            CHECK(run(&dac, op));
        } else {
            CHECK(ok);
        }
        check_completed(&dac, op);
    }
}

static void start_and_stop_survive_every_bus_fault(void)
{
    sweep(CONFIGURE);
    sweep(START_OUTPUT);
    sweep(POWER_DOWN);
    sweep(STOP);
}

static void analog_and_level_changes_survive_every_bus_fault(void)
{
    sweep(GAIN_APPLY);
    sweep(FILTER_APPLY);
    sweep(MUTE);
    sweep(UNMUTE);
    sweep(FADE);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(start_and_stop_survive_every_bus_fault),
        TEST_CASE(analog_and_level_changes_survive_every_bus_fault),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
