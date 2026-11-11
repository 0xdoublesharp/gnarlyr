#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/icons.h"
#include "ui/theme.h"

typedef enum StatusPlayback {
    STATUS_PLAYBACK_NONE,
    STATUS_PLAYBACK_PLAYING,
    STATUS_PLAYBACK_PAUSED
} StatusPlayback;

typedef enum StatusBluetooth {
    STATUS_BLUETOOTH_OFF,
    STATUS_BLUETOOTH_ON,
    STATUS_BLUETOOTH_CONNECTED
} StatusBluetooth;

enum {
    STATUS_BAR_CRITICAL_BATTERY_PERCENT = 5,
    STATUS_BAR_LOW_BATTERY_PERCENT = 15,
    STATUS_BAR_PERCENT_CAPACITY = 12,
    STATUS_BAR_CLOCK_CAPACITY = 24
};

typedef enum StatusShuffle {
    STATUS_SHUFFLE_OFF,
    STATUS_SHUFFLE_SONGS,
    STATUS_SHUFFLE_ALBUMS
} StatusShuffle;

typedef struct StatusBarState {
    StatusPlayback playback;
    StatusBluetooth bluetooth;
    bool battery_known;
    uint8_t battery_percent;
    bool usb_power;
    bool charging;
    bool charging_off;
    bool locked;
    StatusShuffle shuffle;
    const char *percent;
    const char *clock;
} StatusBarState;

typedef struct StatusBatteryLook {
    Color outline;
    Color fill;
    Color bolt;
    bool bolt_halo;
    IconId sign;
} StatusBatteryLook;

Rect status_bar_playback_area(const Theme *theme);
Rect status_bar_bluetooth_area(const Theme *theme);
Rect status_bar_battery_body(const Theme *theme);
Rect status_bar_battery_area(const Theme *theme);
Rect status_bar_lock_area(const Theme *theme);
Rect status_bar_shuffle_area(const Theme *theme, StatusShuffle shuffle);
int status_bar_battery_fill_width(uint8_t percent, int inner_width);
int status_bar_left_end(const Theme *theme, StatusBarState state);
Rect status_bar_clock_area(const Theme *theme, StatusBarState state);
bool status_bar_clock_fits(const Theme *theme, StatusBarState state);
StatusBatteryLook status_bar_battery_look(const Theme *theme, StatusBarState state);
void status_bar_draw(Canvas *canvas, const Theme *theme, StatusBarState state);
