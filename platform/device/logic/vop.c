#include "device/vop.h"

bool vop_wait_idle(volatile uint32_t *registers)
{
    for (int tries = 0; tries < VOP_BUSY_TRIES; tries++) {
        if ((registers[VOP_STATUS] & VOP_STATUS_BUSY) == 0) {
            return true;
        }
    }
    return false;
}

static void set_control(volatile uint32_t *registers, uint32_t value)
{
    if (registers[VOP_CONTROL] != value) {
        vop_wait_idle(registers);
        registers[VOP_CONTROL] = value;
    }
}

void vop_start(volatile uint32_t *registers)
{
    registers[VOP_CONTROL] = VOP_CONTROL_COMMANDS;
    registers[VOP_TIMING] = VOP_TIMING_FIXED;
}

void vop_command(volatile uint32_t *registers, uint8_t command)
{
    set_control(registers, VOP_CONTROL_COMMANDS);
    registers[VOP_COMMAND] = command;
}

void vop_data(volatile uint32_t *registers, uint8_t value)
{
    set_control(registers, VOP_CONTROL_COMMANDS);
    registers[VOP_DATA] = value;
}

void vop_pixels(volatile uint32_t *registers, const uint16_t *pixels, uint32_t count)
{
    set_control(registers, VOP_CONTROL_PIXELS);
    for (uint32_t index = 0; index < count; index++) {
        registers[VOP_DATA] = pixels[index];
        if ((index + 1) % VOP_PIXEL_BURST == 0) {
            vop_wait_idle(registers);
        }
    }
    vop_wait_idle(registers);
}
