#include "device/panel.h"

#include <stddef.h>

enum {
    STEP_END = 0xFF,
    SLEEP_OUT_DELAY_MS = 120,
    COMMAND_COLUMNS = 0x2A,
    COMMAND_ROWS = 0x2B,
    COMMAND_WRITE = 0x2C
};

static const uint8_t ST7796S_STEPS[] = {
    0x11, 0,        0xF0, 1,    0xC3, 0xF0, 1,    0x96, 0x36, 1,    PANEL_ECHO_MADCTL,
    0x3A, 1,        0x55, 0xB4, 1,    0x01, 0xB6, 3,    0x20, 0x02, 0x3B,
    0xB7, 1,        0xC6, 0xB9, 2,    0x02, 0xE0, 0xC0, 2,    0xC0, 0x64,
    0xC1, 1,        0x1D, 0xC2, 1,    0xA7, 0xC5, 1,    0x28, 0xE8, 8,
    0x40, 0x8A,     0x00, 0x00, 0x29, 0x19, 0xA5, 0x33, 0xE0, 14,   0xF0,
    0x0B, 0x12,     0x09, 0x0A, 0x26, 0x39, 0x54, 0x4E, 0x38, 0x13, 0x13,
    0x2E, 0x34,     0xE1, 14,   0xF0, 0x10, 0x15, 0x0D, 0x0C, 0x07, 0x38,
    0x43, 0x4D,     0x3A, 0x16, 0x15, 0x30, 0x35, 0xF0, 1,    0x3C, 0xF0,
    1,    0x69,     0x35, 1,    0x00, 0x29, 0,    0x21, 0,    0x2A, 4,
    0x00, 0x31,     0x01, 0x0E, 0x2B, 4,    0x00, 0x00, 0x01, 0xDF, 0x2C,
    0,    STEP_END,
};

static size_t send_step(const PanelBus *bus, const uint8_t *step)
{
    uint8_t count = step[1];
    bus->command(bus->context, step[0]);
    for (uint8_t index = 0; index < count; index++) {
        bus->data(bus->context, step[2 + index]);
    }
    if (step[0] == 0x11) {
        bus->delay_ms(bus->context, SLEEP_OUT_DELAY_MS);
    }
    return 2u + count;
}

void panel_send_init(const PanelBus *bus)
{
    const uint8_t *step = ST7796S_STEPS;
    while (*step != STEP_END) {
        step += send_step(bus, step);
    }
}

static void send_range(const PanelBus *bus, uint8_t command, int first, int last)
{
    bus->command(bus->context, command);
    bus->data(bus->context, (uint8_t)(first >> 8));
    bus->data(bus->context, (uint8_t)first);
    bus->data(bus->context, (uint8_t)(last >> 8));
    bus->data(bus->context, (uint8_t)last);
}

void panel_begin_write(const PanelBus *bus, PanelWindow window)
{
    int row = window.y + PANEL_FIRST_VISIBLE_ROW;
    send_range(bus, COMMAND_COLUMNS, window.x, window.x + window.width - 1);
    send_range(bus, COMMAND_ROWS, row, row + window.height - 1);
    bus->command(bus->context, COMMAND_WRITE);
}
