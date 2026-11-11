#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bluetooth_link.h"
#include "hal/bluetooth.h"
#include "ui/list_view.h"
#include "ui/navigation.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    BLUETOOTH_SCREEN_HINT_LINE = 0,
    BLUETOOTH_SCREEN_DETAIL_LINES = 4,
    BLUETOOTH_SCREEN_LINE_COUNT = BLUETOOTH_SCREEN_DETAIL_LINES + 1,
    BLUETOOTH_SCREEN_LINE_CAPACITY = 64,
    BLUETOOTH_SCREEN_EVENTS_PER_LINE = HAL_BLUETOOTH_EVENT_CAPACITY / 2
};

typedef struct BluetoothScreen {
    Screen screen;
    const Theme *theme;
    BluetoothLink *link;
    bool details;
    bool pairing;
    HalBluetoothDevice paired;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint8_t nearby_count;
    uint8_t hint_lines;
    uint8_t nearby[HAL_BLUETOOTH_DEVICE_CAPACITY];
    char detail_lines[BLUETOOTH_SCREEN_LINE_COUNT - 1][BLUETOOTH_SCREEN_LINE_CAPACITY];
} BluetoothScreen;

Screen *bluetooth_screen_init(BluetoothScreen *bluetooth, BluetoothLink *link, const Theme *theme);
static inline ScreenAction bluetooth_screen_open(void *bluetooth)
{
    BluetoothScreen *screen = bluetooth;
    screen->details = false;
    return screen_action_push(&screen->screen);
}

static inline ScreenAction bluetooth_screen_open_details(void *bluetooth)
{
    BluetoothScreen *screen = bluetooth;
    screen->details = true;
    return screen_action_push(&screen->screen);
}
const char *bluetooth_screen_summary(const void *bluetooth);
int bluetooth_screen_row_count(const BluetoothScreen *bluetooth);
const char *bluetooth_screen_row(const BluetoothScreen *bluetooth, int index);
const char *bluetooth_screen_row_value(const BluetoothScreen *bluetooth, int index);
const char *bluetooth_screen_line(const BluetoothScreen *bluetooth, int index);
