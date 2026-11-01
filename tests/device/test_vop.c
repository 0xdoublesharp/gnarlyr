#include "check.h"
#include "device/vop.h"

enum {
    REGISTER_COUNT = 16
};

static void a_start_selects_command_mode_and_the_fixed_timing(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    vop_start(registers);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
    CHECK_EQUAL(0x1021, registers[VOP_TIMING]);
}

static void a_command_goes_to_the_command_register_in_command_mode(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    vop_command(registers, 0x2A);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
    CHECK_EQUAL(0x2A, registers[VOP_COMMAND]);
}

static void a_parameter_goes_to_the_data_register_in_command_mode(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    vop_data(registers, 0xC3);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
    CHECK_EQUAL(0xC3, registers[VOP_DATA]);
}

static void pixels_switch_to_pixel_mode_and_the_next_command_switches_back(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    static const uint16_t pixels[] = {0xF800, 0x07E0, 0x001F};
    vop_command(registers, 0x2C);
    vop_pixels(registers, pixels, 3);
    CHECK_EQUAL(0x2, registers[VOP_CONTROL]);
    CHECK_EQUAL(0x001F, registers[VOP_DATA]);
    vop_command(registers, 0x2A);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
}

static void each_pixel_is_one_sixteen_bit_write_to_the_data_register(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    static const uint16_t pixels[] = {0x1234};
    vop_pixels(registers, pixels, 1);
    CHECK_EQUAL(0x1234, registers[VOP_DATA]);
}

static void a_busy_controller_makes_the_wait_give_up(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    CHECK(vop_wait_idle(registers));
    registers[VOP_STATUS] = VOP_STATUS_BUSY;
    CHECK(!vop_wait_idle(registers));
}

static void an_unchanged_mode_is_not_rewritten(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    registers[VOP_CONTROL] = 0x800;
    registers[VOP_STATUS] = VOP_STATUS_BUSY;
    vop_data(registers, 0x11);
    CHECK_EQUAL(0x11, registers[VOP_DATA]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_start_selects_command_mode_and_the_fixed_timing),
        TEST_CASE(a_command_goes_to_the_command_register_in_command_mode),
        TEST_CASE(a_parameter_goes_to_the_data_register_in_command_mode),
        TEST_CASE(pixels_switch_to_pixel_mode_and_the_next_command_switches_back),
        TEST_CASE(each_pixel_is_one_sixteen_bit_write_to_the_data_register),
        TEST_CASE(a_busy_controller_makes_the_wait_give_up),
        TEST_CASE(an_unchanged_mode_is_not_rewritten),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
