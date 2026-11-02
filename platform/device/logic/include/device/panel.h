#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef PANEL_ECHO_MADCTL
#define PANEL_ECHO_MADCTL 0x28
#endif

enum {
    PANEL_FIRST_VISIBLE_ROW = 49
};

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

void panel_send_init(const PanelBus *bus);
void panel_begin_write(const PanelBus *bus, PanelWindow window);
