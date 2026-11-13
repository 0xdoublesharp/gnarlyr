#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/string_builder.h"
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
    STATUS_BAR_CLOCK_CAPACITY = 24,
    STATUS_BAR_VOLUME = 1,
    STATUS_BAR_POSITION = 2,
    STATUS_BAR_CLOCK = 4,
    STATUS_BAR_REPEAT = 8,
    STATUS_BAR_PERCENT = 16
};

typedef enum StatusShuffle {
    STATUS_SHUFFLE_OFF,
    STATUS_SHUFFLE_SONGS,
    STATUS_SHUFFLE_ALBUMS
} StatusShuffle;

typedef struct StatusBarState {
    uint8_t playback;
    uint8_t bluetooth;
    bool battery_known;
    uint8_t battery_percent;
    bool usb_power;
    bool charging;
    bool charging_off;
    bool locked;
    uint8_t shuffle;
    const char *percent;
    const char *clock;
    const char *volume;
    const char *position;
    uint8_t repeat;
    bool segmented;
    int title_width;
} StatusBarState;

typedef struct StatusBarLayout {
    Rect areas[5];
    int shift;
    uint8_t items;
    bool icons_visible;
} StatusBarLayout;

typedef struct StatusQueuePosition {
    uint16_t number;
    uint16_t total;
    const char *prefix;
} StatusQueuePosition;

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
void status_bar_position_text(StringBuilder *builder, StatusQueuePosition position);
void status_bar_layout(const Theme *theme, const StatusBarState *state, StatusBarLayout *layout);
uint8_t status_bar_items(const Theme *theme, const StatusBarState *state);
Rect status_bar_item_area(const Theme *theme, const StatusBarState *state, uint8_t item);
StatusBatteryLook status_bar_battery_look(const Theme *theme, StatusBarState state);
void status_bar_draw(Canvas *canvas, const Theme *theme, StatusBarState state);
