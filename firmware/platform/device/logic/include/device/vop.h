#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    VOP_CONTROL = 0x00 / 4,
    VOP_TIMING = 0x08 / 4,
    VOP_STATUS = 0x24 / 4,
    VOP_COMMAND = 0x28 / 4,
    VOP_DATA = 0x2C / 4,
    VOP_CONTROL_COMMANDS = 0x800,
    VOP_CONTROL_PIXELS = 0x2,
    VOP_TIMING_ENABLE = 0x1000,
    VOP_TIMING_STROBE_SHIFT = 5,
    VOP_STROBE_NS = 20,
    VOP_CYCLE_NS = 70,
    VOP_STATUS_BUSY = 1,
    VOP_BUSY_TRIES = 100001,
    VOP_PIXEL_BURST = 4
};

uint32_t vop_timing_for_mhz(uint32_t mhz);
void vop_start(volatile uint32_t *registers, uint32_t mhz);
void vop_set_clock(volatile uint32_t *registers, uint32_t mhz);
bool vop_wait_idle(volatile uint32_t *registers);
void vop_command(volatile uint32_t *registers, uint8_t command);
void vop_data(volatile uint32_t *registers, uint8_t value);
void vop_pixels(volatile uint32_t *registers, const uint16_t *pixels, uint32_t count);
