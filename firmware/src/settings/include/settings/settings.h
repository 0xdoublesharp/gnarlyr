#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/play_queue.h"

typedef enum GainLevel {
    GAIN_LOW,
    GAIN_HIGH
} GainLevel;

typedef enum OutputJack {
    OUTPUT_AUTO,
    OUTPUT_SINGLE_ENDED,
    OUTPUT_BALANCED
} OutputJack;

typedef enum StartScreen {
    START_MAIN_MENU,
    START_NOW_PLAYING,
    START_ALBUMS,
    START_ARTISTS,
    START_FAVORITES,
    START_PLAYLISTS,
    START_COVER_FLOW
} StartScreen;

typedef enum UsbMode {
    USB_MODE_STORAGE,
    USB_MODE_DAC
} UsbMode;

typedef enum ButtonAction {
    BUTTON_ACTION_NONE,
    BUTTON_ACTION_PLAY_PAUSE,
    BUTTON_ACTION_UP_NEXT,
    BUTTON_ACTION_SEEK,
    BUTTON_ACTION_NEXT,
    BUTTON_ACTION_PREVIOUS,
    BUTTON_ACTION_VOLUME_UP,
    BUTTON_ACTION_VOLUME_DOWN,
    BUTTON_ACTION_HOME
} ButtonAction;

typedef enum PlaybackButton {
    PLAYBACK_VOLUME_UP_TAP,
    PLAYBACK_VOLUME_UP_HOLD,
    PLAYBACK_VOLUME_DOWN_TAP,
    PLAYBACK_VOLUME_DOWN_HOLD,
    PLAYBACK_PLAY_TAP,
    PLAYBACK_PLAY_HOLD,
    PLAYBACK_MENU_TAP,
    PLAYBACK_MENU_HOLD
} PlaybackButton;

enum {
    GAIN_LEVEL_COUNT = GAIN_HIGH + 1,
    OUTPUT_JACK_COUNT = OUTPUT_BALANCED + 1,
    START_SCREEN_COUNT = START_COVER_FLOW + 1,
    USB_MODE_COUNT = USB_MODE_DAC + 1,
    SETTINGS_BRIGHTNESS_MIN = 1,
    SETTINGS_BRIGHTNESS_MAX = 10,
    SETTINGS_VOLUME_MAX = 50,
    SETTINGS_DEFAULT_DAC_VOLUME = 30,
    SCREEN_TIMEOUT_OPTION_COUNT = 7,
    SLEEP_TIMER_OPTION_COUNT = 7,
    AUTO_OFF_OPTION_COUNT = 4,
    BUTTON_ACTION_COUNT = BUTTON_ACTION_HOME + 1,
    PLAYBACK_BUTTON_COUNT = PLAYBACK_MENU_HOLD + 1,
    SETTINGS_LEGACY_FILE_SIZE = 24,
    SETTINGS_FILE_SIZE = 32
};

typedef struct Settings {
    uint8_t brightness;
    uint8_t screen_timeout;
    GainLevel gain;
    OutputJack output;
    StartScreen start_screen;
    ShuffleMode shuffle;
    RepeatMode repeat;
    uint8_t volume;
    uint8_t auto_off;
    UsbMode usb_mode;
    uint8_t dac_volume;
    uint8_t button_actions[PLAYBACK_BUTTON_COUNT];
} Settings;

Settings settings_defaults(void);
ButtonAction settings_default_button_action(PlaybackButton button);
uint32_t settings_timeout_option_seconds(int option);
uint32_t settings_sleep_option_minutes(int option);
uint32_t settings_screen_timeout_ms(const Settings *settings);
uint32_t settings_auto_off_option_minutes(int option);
uint32_t settings_auto_off_ms(const Settings *settings);
void settings_encode(const Settings *settings, uint8_t *bytes);
bool settings_decode(const uint8_t *bytes, uint32_t length, Settings *settings);
