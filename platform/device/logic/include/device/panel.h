#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef PANEL_ECHO_MADCTL
#define PANEL_ECHO_MADCTL 0x28
#endif

enum {
    PANEL_MINI_FIRST_VISIBLE_ROW = 35,
    PANEL_ECHO_FIRST_VISIBLE_ROW = 49,
#ifdef ECHO_BOARD_ECHO
    PANEL_FIRST_VISIBLE_ROW = PANEL_ECHO_FIRST_VISIBLE_ROW,
#else
    PANEL_FIRST_VISIBLE_ROW = PANEL_MINI_FIRST_VISIBLE_ROW,
#endif
    PANEL_BUS_DATA_SHIFT = 16,
    PANEL_BUS_WR = 1u << 24,
    PANEL_BUS_DC = 1u << 25
};

typedef enum PanelVariant {
    PANEL_VARIANT_A,
    PANEL_VARIANT_B,
    PANEL_VARIANT_ST7796S
} PanelVariant;

typedef struct PanelBus {
    void (*command)(void *context, uint8_t command);
    void (*data)(void *context, uint8_t value);
    void (*pixels)(void *context, const uint16_t *pixels, uint32_t count);
    void (*delay_ms)(void *context, uint32_t milliseconds);
    void *context;
} PanelBus;

typedef struct PanelWindow {
    int x;
    int y;
    int width;
    int height;
} PanelWindow;

void panel_send_init(const PanelBus *bus, PanelVariant variant);
void panel_begin_write(const PanelBus *bus, PanelWindow window);
void panel_begin_write_at(const PanelBus *bus, PanelWindow window, int first_row);
uint32_t panel_bus_word(uint32_t shadow, uint8_t value, bool data);
