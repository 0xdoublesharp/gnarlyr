#include <stdbool.h>
#include <stddef.h>

#include "check.h"
#include "device/dac_sequence.h"

enum {
    LEFT = 0x60,
    RIGHT = 0x66,
    OP_CAPACITY = 512,
    IDENTITY_PAGE = 0x0100,
    IDENTITY_REGISTERS = 5,
    START_VOLUME = 0x5E
};

static const uint32_t IDENTITY_MAPS[IDENTITY_REGISTERS] = {0x010000, 0x010001, 0x010002, 0x010004,
                                                           0x010005};
static const uint8_t IDENTITY_VALUES[2][IDENTITY_REGISTERS] = {{0x43, 0x19, 0x80, 0xA0, 0x01},
                                                               {0x43, 0x13, 0x10, 0xB2, 0x07}};

typedef enum OpKind {
    OP_WRITE,
    OP_READ,
    OP_PIN,
    OP_DELAY
} OpKind;

typedef struct Op {
    OpKind kind;
    uint8_t address;
    uint32_t map;
    uint32_t value;
} Op;

typedef struct FakeBus {
    Op ops[OP_CAPACITY];
    int count;
    uint8_t status[2];
    uint8_t power_control[2];
    uint8_t identity_failures[2];
    uint8_t done_after[2];
    uint32_t powered_down_us[2];
    uint32_t elapsed_us;
    int transfers;
    int fail_transfer;
    bool silent;
} FakeBus;

static FakeBus fake;

static int chip_of(uint8_t address)
{
    return address == LEFT ? 0 : 1;
}

static void record(Op op)
{
    if (fake.count < OP_CAPACITY) {
        fake.ops[fake.count] = op;
    }
    fake.count++;
}

static bool fake_write(void *context, uint8_t address, uint32_t map, uint8_t value)
{
    (void)context;
    fake.transfers++;
    if (fake.silent || fake.transfers == fake.fail_transfer) {
        return false;
    }
    record((Op){OP_WRITE, address, map, value});
    if (map == 0x020000) {
        fake.power_control[chip_of(address)] = value;
        fake.powered_down_us[chip_of(address)] = fake.elapsed_us;
    }
    return true;
}

static uint8_t read_status_register(int chip)
{
    if ((fake.power_control[chip] & 0x10) == 0) {
        return fake.status[chip];
    }
    bool done = fake.elapsed_us - fake.powered_down_us[chip] >= fake.done_after[chip] * 1000u;
    return (uint8_t)(fake.status[chip] | (done ? 0x01 : 0x00));
}

static bool is_identity_map(uint32_t map)
{
    return map >> 8 == IDENTITY_PAGE;
}

static bool read_identity(uint8_t address, uint32_t map, uint8_t *value)
{
    *value = 0xEE;
    for (int index = 0; index < IDENTITY_REGISTERS; index++) {
        if (map == IDENTITY_MAPS[index]) {
            *value = IDENTITY_VALUES[chip_of(address)][index];
            return ((fake.identity_failures[chip_of(address)] >> index) & 1u) == 0;
        }
    }
    return true;
}

static bool fake_read(void *context, uint8_t address, uint32_t map, uint8_t *value)
{
    (void)context;
    fake.transfers++;
    if (fake.silent || fake.transfers == fake.fail_transfer) {
        return false;
    }
    record((Op){OP_READ, address, map, 0});
    if (is_identity_map(map)) {
        return read_identity(address, map, value);
    }
    int chip = chip_of(address);
    *value = map == 0x0F0000 ? read_status_register(chip) : fake.power_control[chip];
    return true;
}

static void fake_pin(void *context, DacPin pin, bool level)
{
    (void)context;
    record((Op){OP_PIN, 0, (uint32_t)pin, level ? 1u : 0u});
}

static void fake_delay(void *context, uint32_t microseconds)
{
    (void)context;
    record((Op){OP_DELAY, 0, 0, microseconds});
    fake.elapsed_us += microseconds;
}

static DacBus fresh_bus(void)
{
    fake = (FakeBus){.count = 0};
    return (DacBus){fake_write, fake_read, fake_pin, fake_delay, NULL};
}

static int cursor;

static bool next_is(Op expected)
{
    if (cursor >= fake.count) {
        return false;
    }
    Op actual = fake.ops[cursor++];
    return actual.kind == expected.kind && actual.address == expected.address &&
           actual.map == expected.map && actual.value == expected.value;
}

static bool next_pin(DacPin pin, bool level)
{
    return next_is((Op){OP_PIN, 0, (uint32_t)pin, level ? 1u : 0u});
}

static bool next_delay_ms(uint32_t milliseconds)
{
    return next_is((Op){OP_DELAY, 0, 0, milliseconds * 1000u});
}

static bool next_both(uint32_t map, uint8_t value)
{
    return next_is((Op){OP_WRITE, LEFT, map, value}) && next_is((Op){OP_WRITE, RIGHT, map, value});
}

static bool next_one(uint8_t address, uint32_t map, uint8_t value)
{
    return next_is((Op){OP_WRITE, address, map, value});
}

static bool next_release(uint8_t address, uint8_t before, uint8_t bit)
{
    return next_is((Op){OP_DELAY, 0, 0, 100}) && next_is((Op){OP_READ, address, 0x020000, 0}) &&
           next_one(address, 0x020000, (uint8_t)(before & ~bit));
}

static void the_power_pins_pulse_reset_as_in_the_facts(void)
{
    DacBus bus = fresh_bus();
    cursor = 0;
    dac_power_pins(&bus);
    CHECK(next_pin(DAC_PIN_MUTE, true));
    CHECK(next_pin(DAC_PIN_ENABLE_A, true));
    CHECK(next_pin(DAC_PIN_ENABLE_B, true));
    CHECK(next_delay_ms(2));
    CHECK(next_pin(DAC_PIN_RESET, false));
    CHECK(next_delay_ms(1));
    CHECK(next_pin(DAC_PIN_RESET, true));
    CHECK(next_delay_ms(8));
    CHECK(next_pin(DAC_PIN_RESET, false));
    CHECK(next_delay_ms(2));
    CHECK_EQUAL(cursor, fake.count);
}

static void the_probe_writes_map_zero_to_both_chips(void)
{
    DacBus bus = fresh_bus();
    cursor = 0;
    CHECK(dac_probe(&bus));
    CHECK(next_both(0x000000, 0x00));
    CHECK_EQUAL(cursor, fake.count);
}

static void a_silent_bus_fails_the_probe(void)
{
    DacBus bus = fresh_bus();
    fake.silent = true;
    CHECK(!dac_probe(&bus));
}

static bool next_identity_reads(uint8_t address, int count)
{
    for (int index = 0; index < count; index++) {
        if (!next_is((Op){OP_READ, address, IDENTITY_MAPS[index], 0})) {
            return false;
        }
    }
    return true;
}

static bool holds(const DacIdentity *chip, uint8_t mask, const uint8_t *values)
{
    for (int index = 0; index < IDENTITY_REGISTERS; index++) {
        uint8_t expected = ((mask >> index) & 1u) != 0 ? values[index] : 0;
        if (chip->bytes[index] != expected) {
            return false;
        }
    }
    return chip->source == DAC_IDENTITY_READ && chip->read_mask == mask;
}

static void identification_only_reads_the_five_identity_registers_of_each_chip(void)
{
    DacIdentity chips[DAC_IDENTITY_CHIPS];
    DacBus bus = fresh_bus();
    cursor = 0;
    dac_read_identity(&bus, chips);
    CHECK(next_identity_reads(LEFT, IDENTITY_REGISTERS));
    CHECK(next_identity_reads(RIGHT, IDENTITY_REGISTERS));
    CHECK_EQUAL(cursor, fake.count);
    CHECK(holds(&chips[0], DAC_IDENTITY_ALL_READ, IDENTITY_VALUES[0]));
    CHECK(holds(&chips[1], DAC_IDENTITY_ALL_READ, IDENTITY_VALUES[1]));
}

static void identification_leaves_a_chip_at_its_first_failed_register(void)
{
    DacIdentity chips[DAC_IDENTITY_CHIPS];
    DacBus bus = fresh_bus();
    fake.identity_failures[0] = 0x04;
    cursor = 0;
    dac_read_identity(&bus, chips);
    CHECK(next_identity_reads(LEFT, 3));
    CHECK(next_identity_reads(RIGHT, IDENTITY_REGISTERS));
    CHECK_EQUAL(cursor, fake.count);
    CHECK(holds(&chips[0], 0x03, IDENTITY_VALUES[0]));
    CHECK(holds(&chips[1], DAC_IDENTITY_ALL_READ, IDENTITY_VALUES[1]));
}

static void a_chip_that_does_not_answer_is_asked_once_and_keeps_no_bytes(void)
{
    DacIdentity chips[DAC_IDENTITY_CHIPS];
    DacBus bus = fresh_bus();
    fake.identity_failures[0] = DAC_IDENTITY_ALL_READ;
    fake.identity_failures[1] = DAC_IDENTITY_ALL_READ;
    cursor = 0;
    dac_read_identity(&bus, chips);
    CHECK(next_identity_reads(LEFT, 1));
    CHECK(next_identity_reads(RIGHT, 1));
    CHECK_EQUAL(cursor, fake.count);
    CHECK(holds(&chips[0], 0, IDENTITY_VALUES[0]));
    CHECK(holds(&chips[1], 0, IDENTITY_VALUES[1]));
}

static Op reference[OP_CAPACITY];
static int reference_count;

static void start_up(const DacBus *bus, bool identify)
{
    DacIdentity chips[DAC_IDENTITY_CHIPS];
    dac_power_pins(bus);
    if (identify) {
        dac_read_identity(bus, chips);
    }
    CHECK(dac_probe(bus));
    CHECK_EQUAL(DAC_READY, dac_configure(bus, 1));
    dac_output_on(bus, START_VOLUME);
}

static void keep_reference_start_up(void)
{
    DacBus bus = fresh_bus();
    start_up(&bus, false);
    CHECK(fake.count <= OP_CAPACITY);
    reference_count = fake.count;
    for (int index = 0; index < reference_count; index++) {
        reference[index] = fake.ops[index];
    }
}

static bool same_op(Op first, Op second)
{
    return first.kind == second.kind && first.address == second.address &&
           first.map == second.map && first.value == second.value;
}

static bool start_up_matches_reference(uint8_t left_failures, uint8_t right_failures, int reads)
{
    DacBus bus = fresh_bus();
    fake.identity_failures[0] = left_failures;
    fake.identity_failures[1] = right_failures;
    start_up(&bus, true);
    int matched = 0;
    int identity_reads = 0;
    for (int index = 0; index < fake.count && index < OP_CAPACITY; index++) {
        Op op = fake.ops[index];
        if (op.kind == OP_READ && is_identity_map(op.map)) {
            identity_reads++;
        } else if (matched >= reference_count || !same_op(op, reference[matched++])) {
            return false;
        }
    }
    return matched == reference_count && identity_reads == reads &&
           fake.count == reference_count + reads;
}

static void start_up_writes_pins_and_waits_are_unchanged_by_identification(void)
{
    keep_reference_start_up();
    CHECK(reference_count > 0);
    CHECK(start_up_matches_reference(0, 0, 10));
    CHECK(start_up_matches_reference(0x04, 0x10, 8));
    CHECK(start_up_matches_reference(DAC_IDENTITY_ALL_READ, DAC_IDENTITY_ALL_READ, 2));
}

static void the_reference_start_up_itself_reads_no_identity_register(void)
{
    keep_reference_start_up();
    for (int index = 0; index < reference_count; index++) {
        CHECK(reference[index].kind != OP_READ || !is_identity_map(reference[index].map));
    }
}

static bool next_mono_table(void)
{
    static const uint8_t OFFSETS[6] = {0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x10};
    static const uint8_t VALUES[4][6] = {
        {0x2F, 0xB4, 0x25, 0x80, 0x55, 0xFA},
        {0x2F, 0xB4, 0x25, 0x80, 0x4E, 0xFD},
        {0x2F, 0xB4, 0x25, 0x80, 0x7C, 0xFD},
        {0x2F, 0xB4, 0x25, 0x80, 0x52, 0xFE},
    };
    for (int group = 0; group < 4; group++) {
        for (int item = 0; item < 6; item++) {
            uint32_t map = 0x180000u + (uint32_t)group * 0x10u + OFFSETS[item];
            if (!next_both(map, VALUES[group][item])) {
                return false;
            }
        }
    }
    return true;
}

static void the_register_sequence_follows_the_facts(void)
{
    static const uint8_t BLOCK_4[10] = {0x01, 0x00, 0x08, 0x00, 0x1F, 0x00, 0x3F, 0x00, 0x0C, 0x0A};
    DacBus bus = fresh_bus();
    cursor = 0;
    CHECK_EQUAL(DAC_READY, dac_configure(&bus, 1));
    CHECK(next_both(0x0B0000, 0x1F));
    CHECK(next_delay_ms(80));
    CHECK(next_pin(DAC_PIN_AMP, true));
    CHECK(next_both(0x020052, 0x02));
    CHECK(next_is((Op){OP_READ, LEFT, 0x0F0000, 0}));
    CHECK(next_is((Op){OP_READ, RIGHT, 0x0F0000, 0}));
    CHECK(next_both(0x0F0010, 0xE7));
    CHECK(next_both(0x020000, 0xF6));
    CHECK(next_both(0x01000C, 0x04));
    for (uint32_t index = 0; index < 10; index++) {
        CHECK(next_both(0x040010 + index, BLOCK_4[index]));
    }
    CHECK(next_both(0x050000, 0x00));
    CHECK(next_both(0x050001, 0x00));
    CHECK(next_one(LEFT, 0x05000A, 0x07) && next_one(LEFT, 0x05000B, 0x07));
    CHECK(next_one(RIGHT, 0x05000A, 0x0F) && next_one(RIGHT, 0x05000B, 0x0F));
    CHECK(next_both(0x090000, 0x02));
    CHECK(next_both(0x090001, 0xFF));
    CHECK(next_both(0x090002, 0xFF));
    CHECK(next_both(0x090003, 0xEC));
    CHECK(next_both(0x090004, 0x00));
    CHECK(next_mono_table());
    CHECK(next_both(0x0B0000, 0x1F));
    CHECK(next_delay_ms(80));
    CHECK(next_both(0x080000, 0x10));
    CHECK(next_both(0x0D0000, 0x04));
    CHECK(next_both(0x0D0001, 0x00));
    CHECK(next_both(0x0F0010, 0x87));
    CHECK(next_both(0x0F0011, 0x07));
    CHECK(next_both(0x010006, 0x04));
    CHECK(next_both(0x01000D, 0x02));
    CHECK(next_both(0x010010, 0x99));
    CHECK(next_both(0x180000, 0x45));
    CHECK(next_both(0x010010, 0x00));
    CHECK(next_release(LEFT, 0xF6, 0x40));
    CHECK(next_release(RIGHT, 0xF6, 0x40));
    CHECK(next_both(0x010010, 0x99));
    CHECK(next_both(0x080032, 0x20));
    CHECK(next_release(LEFT, 0xB6, 0x10));
    CHECK(next_release(RIGHT, 0xB6, 0x10));
    CHECK(next_delay_ms(12));
    CHECK(next_both(0x080032, 0x00));
    CHECK(next_both(0x010010, 0x00));
    CHECK(next_both(0x01000B, 0x01));
    CHECK(next_delay_ms(10));
    CHECK_EQUAL(cursor, fake.count);
    CHECK_EQUAL(0xA6, fake.power_control[0]);
    CHECK_EQUAL(0xA6, fake.power_control[1]);
}

static void both_chips_reporting_not_ready_stops_the_sequence(void)
{
    DacBus bus = fresh_bus();
    fake.status[0] = 0x03;
    fake.status[1] = 0x03;
    CHECK_EQUAL(DAC_NOT_READY, dac_configure(&bus, 1));
    CHECK_EQUAL(OP_READ, fake.ops[fake.count - 1].kind);
}

static void one_chip_reporting_not_ready_carries_on(void)
{
    DacBus bus = fresh_bus();
    fake.status[0] = 0x03;
    CHECK_EQUAL(DAC_READY, dac_configure(&bus, 1));
}

static void a_silent_bus_reports_no_reply(void)
{
    DacBus bus = fresh_bus();
    fake.silent = true;
    CHECK_EQUAL(DAC_NO_REPLY, dac_configure(&bus, 1));
}

static void every_failed_configuration_transfer_reports_no_reply(void)
{
    DacBus bus = fresh_bus();
    CHECK_EQUAL(DAC_READY, dac_configure(&bus, 1));
    int transfers = fake.transfers;
    CHECK(transfers > 20);
    for (int failure = 1; failure <= transfers; failure++) {
        bus = fresh_bus();
        fake.fail_transfer = failure;
        CHECK_EQUAL(DAC_NO_REPLY, dac_configure(&bus, 1));
        CHECK(fake.transfers >= failure);
    }
}

static DacBus playing_bus(uint8_t left_done_after, uint8_t right_done_after);

static void every_failed_output_shutdown_transfer_stops_safely(void)
{
    DacBus bus = playing_bus(0, 0);
    dac_output_off(&bus);
    int transfers = fake.transfers;
    CHECK(transfers > 8);
    for (int failure = 1; failure <= transfers; failure++) {
        bus = playing_bus(0, 0);
        fake.fail_transfer = failure;
        dac_output_off(&bus);
        CHECK(fake.transfers >= failure);
    }
}

static void output_on_unmutes_and_restores_the_channel_values(void)
{
    DacBus bus = fresh_bus();
    cursor = 0;
    dac_output_on(&bus, 0x5E);
    CHECK(next_pin(DAC_PIN_ENABLE_A, true));
    CHECK(next_pin(DAC_PIN_MUTE, false));
    CHECK(next_pin(DAC_PIN_AMP, true));
    CHECK(next_one(LEFT, 0x05000A, 0x07) && next_one(LEFT, 0x05000B, 0x07));
    CHECK(next_one(RIGHT, 0x05000A, 0x0F) && next_one(RIGHT, 0x05000B, 0x0F));
    CHECK(next_both(0x090001, 0x5E));
    CHECK(next_both(0x090002, 0x5E));
    CHECK(next_delay_ms(10));
    CHECK_EQUAL(cursor, fake.count);
}

static DacBus playing_bus(uint8_t left_done_after, uint8_t right_done_after)
{
    DacBus bus = fresh_bus();
    fake.power_control[0] = 0xA6;
    fake.power_control[1] = 0xA6;
    fake.done_after[0] = left_done_after;
    fake.done_after[1] = right_done_after;
    cursor = 0;
    return bus;
}

static bool next_status_read(uint8_t address)
{
    return next_is((Op){OP_READ, address, 0x0F0000, 0});
}

static bool next_power_down(uint8_t address, uint8_t before, uint8_t bit)
{
    return next_is((Op){OP_DELAY, 0, 0, 100}) && next_is((Op){OP_READ, address, 0x020000, 0}) &&
           next_one(address, 0x020000, (uint8_t)(before | bit));
}

static bool next_muted_output(void)
{
    return next_pin(DAC_PIN_MUTE, true) && next_pin(DAC_PIN_ENABLE_A, true) &&
           next_both(0x090001, 0xFF) && next_both(0x090002, 0xFF);
}

static bool next_supplies_off(void)
{
    return next_both(0x0B0000, 0x00) && next_delay_ms(10) && next_pin(DAC_PIN_AMP, false) &&
           next_pin(DAC_PIN_RESET, true) && next_pin(DAC_PIN_ENABLE_B, false);
}

static void output_off_mutes_then_powers_the_outputs_down_before_the_serial_port(void)
{
    DacBus bus = playing_bus(0, 0);
    dac_output_off(&bus);
    CHECK(next_muted_output());
    CHECK(next_status_read(LEFT) && next_status_read(RIGHT));
    CHECK(next_power_down(LEFT, 0xA6, 0x10));
    CHECK(next_power_down(RIGHT, 0xA6, 0x10));
    CHECK(next_status_read(LEFT) && next_status_read(RIGHT));
    CHECK(next_power_down(LEFT, 0xB6, 0x40));
    CHECK(next_power_down(RIGHT, 0xB6, 0x40));
    CHECK(next_supplies_off());
    CHECK_EQUAL(cursor, fake.count);
    CHECK_EQUAL(0xF6, fake.power_control[0]);
    CHECK_EQUAL(0xF6, fake.power_control[1]);
}

static void output_off_waits_for_both_chips_to_finish_their_output_ramp(void)
{
    DacBus bus = playing_bus(3, 5);
    dac_output_off(&bus);
    int waits = 0;
    int serial_port_downs = 0;
    for (int index = 0; index < fake.count && serial_port_downs == 0; index++) {
        Op op = fake.ops[index];
        waits += op.kind == OP_DELAY && op.value == 1000 ? 1 : 0;
        serial_port_downs += op.kind == OP_WRITE && op.map == 0x020000 && (op.value & 0x40) != 0;
    }
    CHECK_EQUAL(5, waits);
    CHECK_EQUAL(0xF6, fake.power_control[0]);
}

static void a_chip_that_never_reports_its_ramp_done_is_given_up_after_150_ms(void)
{
    DacBus bus = playing_bus(255, 255);
    dac_output_off(&bus);
    int waits = 0;
    for (int index = 0; index < fake.count; index++) {
        waits += fake.ops[index].kind == OP_DELAY && fake.ops[index].value == 1000 ? 1 : 0;
    }
    CHECK_EQUAL(150, waits);
    CHECK_EQUAL(0xF6, fake.power_control[1]);
    CHECK_EQUAL(OP_PIN, fake.ops[fake.count - 1].kind);
    CHECK_EQUAL(DAC_PIN_ENABLE_B, fake.ops[fake.count - 1].map);
}

static void muting_holds_the_pin_and_mutes_both_chips(void)
{
    DacBus bus = fresh_bus();
    cursor = 0;
    dac_set_muted(&bus, true, 0x5E);
    CHECK(next_pin(DAC_PIN_MUTE, true));
    CHECK(next_both(0x090001, 0xFF));
    CHECK(next_both(0x090002, 0xFF));
    dac_set_muted(&bus, false, 0x5E);
    CHECK(next_both(0x090001, 0x5E));
    CHECK(next_both(0x090002, 0x5E));
    CHECK(next_pin(DAC_PIN_MUTE, false));
    CHECK_EQUAL(cursor, fake.count);
}

static void setting_the_volume_writes_both_pcm_volume_registers(void)
{
    DacBus bus = fresh_bus();
    cursor = 0;
    dac_set_volume(&bus, 0x30);
    CHECK(next_both(0x090001, 0x30));
    CHECK(next_both(0x090002, 0x30));
    CHECK_EQUAL(cursor, fake.count);
}

static void the_gain_setting_writes_the_headphone_gain_register(void)
{
    DacBus bus = fresh_bus();
    cursor = 0;
    dac_set_gain(&bus, true);
    CHECK(next_both(0x080000, 0x31));
    dac_set_gain(&bus, false);
    CHECK(next_both(0x080000, 0x10));
    CHECK_EQUAL(cursor, fake.count);
}

static void volume_levels_map_onto_the_pcm_volume_scale(void)
{
    CHECK_EQUAL(0xFF, dac_volume_value(0, DAC_CURVE_SINGLE_ENDED));
    CHECK_EQUAL(0x76, dac_volume_value(1, DAC_CURVE_SINGLE_ENDED));
    CHECK_EQUAL(0x60, dac_volume_value(12, DAC_CURVE_SINGLE_ENDED));
    CHECK_EQUAL(0x3C, dac_volume_value(30, DAC_CURVE_SINGLE_ENDED));
    CHECK_EQUAL(0x02, dac_volume_value(59, DAC_CURVE_SINGLE_ENDED));
    CHECK_EQUAL(0x00, dac_volume_value(60, DAC_CURVE_SINGLE_ENDED));
    CHECK_EQUAL(0x00, dac_volume_value(80, DAC_CURVE_SINGLE_ENDED));
}

static void the_balanced_curve_sits_ten_units_lower(void)
{
    CHECK_EQUAL(0xFF, dac_volume_value(0, DAC_CURVE_BALANCED));
    CHECK_EQUAL(0x80, dac_volume_value(1, DAC_CURVE_BALANCED));
    CHECK_EQUAL(0x6A, dac_volume_value(12, DAC_CURVE_BALANCED));
    CHECK_EQUAL(0x0C, dac_volume_value(59, DAC_CURVE_BALANCED));
    CHECK_EQUAL(0x0A, dac_volume_value(60, DAC_CURVE_BALANCED));
}

static void a_volume_ramp_moves_six_units_a_step_and_lands_on_the_target(void)
{
    CHECK_EQUAL(0xF9, dac_ramp_next(0xFF, 0x02));
    CHECK_EQUAL(0x02, dac_ramp_next(0x05, 0x02));
    CHECK_EQUAL(0x08, dac_ramp_next(0x02, 0xFF));
    CHECK_EQUAL(0xFF, dac_ramp_next(0xFC, 0xFF));
    CHECK_EQUAL(0x30, dac_ramp_next(0x30, 0x30));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_power_pins_pulse_reset_as_in_the_facts),
        TEST_CASE(the_probe_writes_map_zero_to_both_chips),
        TEST_CASE(a_silent_bus_fails_the_probe),
        TEST_CASE(identification_only_reads_the_five_identity_registers_of_each_chip),
        TEST_CASE(identification_leaves_a_chip_at_its_first_failed_register),
        TEST_CASE(a_chip_that_does_not_answer_is_asked_once_and_keeps_no_bytes),
        TEST_CASE(start_up_writes_pins_and_waits_are_unchanged_by_identification),
        TEST_CASE(the_reference_start_up_itself_reads_no_identity_register),
        TEST_CASE(the_register_sequence_follows_the_facts),
        TEST_CASE(both_chips_reporting_not_ready_stops_the_sequence),
        TEST_CASE(one_chip_reporting_not_ready_carries_on),
        TEST_CASE(a_silent_bus_reports_no_reply),
        TEST_CASE(every_failed_configuration_transfer_reports_no_reply),
        TEST_CASE(every_failed_output_shutdown_transfer_stops_safely),
        TEST_CASE(output_on_unmutes_and_restores_the_channel_values),
        TEST_CASE(output_off_mutes_then_powers_the_outputs_down_before_the_serial_port),
        TEST_CASE(output_off_waits_for_both_chips_to_finish_their_output_ramp),
        TEST_CASE(a_chip_that_never_reports_its_ramp_done_is_given_up_after_150_ms),
        TEST_CASE(muting_holds_the_pin_and_mutes_both_chips),
        TEST_CASE(setting_the_volume_writes_both_pcm_volume_registers),
        TEST_CASE(the_gain_setting_writes_the_headphone_gain_register),
        TEST_CASE(volume_levels_map_onto_the_pcm_volume_scale),
        TEST_CASE(the_balanced_curve_sits_ten_units_lower),
        TEST_CASE(a_volume_ramp_moves_six_units_a_step_and_lands_on_the_target),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
