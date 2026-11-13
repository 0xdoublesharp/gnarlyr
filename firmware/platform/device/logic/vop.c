#include "device/vop.h"

#include "core/seam.h"

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

static uint32_t cycles_for(uint32_t mhz, uint32_t nanoseconds)
{
    uint32_t cycles = (mhz * nanoseconds + 999u) / 1000u;
    return cycles == 0 ? 1u : cycles;
}

CORE_COLD uint32_t vop_timing_for_mhz(uint32_t mhz)
{
    uint32_t strobe = cycles_for(mhz, VOP_STROBE_NS);
    uint32_t cycle = cycles_for(mhz, VOP_CYCLE_NS);
    uint32_t rest = strobe + 2u < cycle ? cycle - 1u - strobe : 1u;
    return rest | strobe << VOP_TIMING_STROBE_SHIFT | VOP_TIMING_ENABLE;
}

CORE_COLD void vop_start(volatile uint32_t *registers, uint32_t mhz)
{
    registers[VOP_CONTROL] = VOP_CONTROL_COMMANDS;
    registers[VOP_TIMING] = vop_timing_for_mhz(mhz);
}

void vop_set_clock(volatile uint32_t *registers, uint32_t mhz)
{
    vop_wait_idle(registers);
    registers[VOP_TIMING] = vop_timing_for_mhz(mhz);
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

uint32_t vop_fifo_size(uint32_t width, uint32_t height)
{
    bool odd = (width & 1u) != 0;
    uint32_t line = odd ? height : width;
    uint32_t lines = odd ? width : height;
    return (lines - 1u) << VOP_SIZE_HEIGHT_SHIFT | (line - 1u);
}

bool vop_begin_fifo(volatile uint32_t *registers, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || ((width * height) & 1u) != 0) {
        return false;
    }
    registers[VOP_INTERRUPT_CLEAR] = VOP_INTERRUPTS_ALL;
    registers[VOP_FIFO_LEVELS] = VOP_FIFO_WATERMARKS;
    registers[VOP_LCD_SIZE] = vop_fifo_size(width, height);
    vop_wait_idle(registers);
    registers[VOP_CONTROL] = VOP_CONTROL_FIFO;
    return true;
}

void vop_end_fifo(volatile uint32_t *registers)
{
    vop_wait_idle(registers);
    registers[VOP_CONTROL] = VOP_CONTROL_COMMANDS;
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
