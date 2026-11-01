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
    VOP_TIMING_FIXED = 0x1021,
    VOP_STATUS_BUSY = 1,
    VOP_BUSY_TRIES = 100001,
    VOP_PIXEL_BURST = 4
};

void vop_start(volatile uint32_t *registers);
bool vop_wait_idle(volatile uint32_t *registers);
void vop_command(volatile uint32_t *registers, uint8_t command);
void vop_data(volatile uint32_t *registers, uint8_t value);
void vop_pixels(volatile uint32_t *registers, const uint16_t *pixels, uint32_t count);
