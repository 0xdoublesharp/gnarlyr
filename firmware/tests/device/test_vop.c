#include "check.h"
#include "device/vop.h"

enum {
    REGISTER_COUNT = 16
};

static void a_start_selects_command_mode_and_the_timing_for_the_clock(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    vop_start(registers, 96);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
    CHECK_EQUAL(0x1044, registers[VOP_TIMING]);
}

static void the_crystal_clock_gives_the_stock_echo_timing(void)
{
    CHECK_EQUAL(0x1021, vop_timing_for_mhz(24));
}

static void a_faster_clock_stretches_the_write_strobe_and_cycle(void)
{
    CHECK_EQUAL(0x1044, vop_timing_for_mhz(96));
    CHECK_EQUAL(0x1089, vop_timing_for_mhz(200));
}

static void a_clock_change_rewrites_only_the_timing(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    vop_start(registers, 24);
    vop_set_clock(registers, 200);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
    CHECK_EQUAL(0x1089, registers[VOP_TIMING]);
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

static void a_fifo_window_declares_its_lines_and_selects_fifo_mode(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    CHECK(vop_begin_fifo(registers, 480, 23));
    CHECK_EQUAL(0x16, registers[VOP_CONTROL]);
    CHECK_EQUAL(0x808, registers[VOP_FIFO_LEVELS]);
    CHECK_EQUAL(7, registers[VOP_INTERRUPT_CLEAR]);
    CHECK_EQUAL((22u << 12) | 479u, registers[VOP_LCD_SIZE]);
    vop_end_fifo(registers);
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
}

static void an_odd_width_is_declared_as_lines_of_the_height(void)
{
    CHECK_EQUAL((220u << 12) | 1u, vop_fifo_size(221, 2));
}

static void a_window_with_an_odd_pixel_count_stays_with_the_cpu(void)
{
    uint32_t registers[REGISTER_COUNT] = {0};
    registers[VOP_CONTROL] = 0x800;
    CHECK(!vop_begin_fifo(registers, 3, 3));
    CHECK(!vop_begin_fifo(registers, 0, 4));
    CHECK_EQUAL(0x800, registers[VOP_CONTROL]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_start_selects_command_mode_and_the_timing_for_the_clock),
        TEST_CASE(the_crystal_clock_gives_the_stock_echo_timing),
        TEST_CASE(a_faster_clock_stretches_the_write_strobe_and_cycle),
        TEST_CASE(a_clock_change_rewrites_only_the_timing),
        TEST_CASE(a_command_goes_to_the_command_register_in_command_mode),
        TEST_CASE(a_parameter_goes_to_the_data_register_in_command_mode),
        TEST_CASE(pixels_switch_to_pixel_mode_and_the_next_command_switches_back),
        TEST_CASE(each_pixel_is_one_sixteen_bit_write_to_the_data_register),
        TEST_CASE(a_busy_controller_makes_the_wait_give_up),
        TEST_CASE(an_unchanged_mode_is_not_rewritten),
        TEST_CASE(a_fifo_window_declares_its_lines_and_selects_fifo_mode),
        TEST_CASE(an_odd_width_is_declared_as_lines_of_the_height),
        TEST_CASE(a_window_with_an_odd_pixel_count_stays_with_the_cpu),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
