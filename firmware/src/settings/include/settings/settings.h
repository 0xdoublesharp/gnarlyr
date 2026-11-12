#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/parametric.h"

#include "core/replay_gain.h"
#include "playback/play_queue.h"

typedef enum NowPlayingStyle {
    NOW_PLAYING_COVER,
    NOW_PLAYING_CASSETTE,
    NOW_PLAYING_VU,
    NOW_PLAYING_SPECTRUM
} NowPlayingStyle;

typedef enum GainLevel {
    GAIN_LOW,
    GAIN_HIGH
} GainLevel;

typedef enum DacFilter {
    DAC_FILTER_FAST_LOW_LATENCY,
    DAC_FILTER_FAST_PHASE_COMPENSATED,
    DAC_FILTER_SLOW_LOW_LATENCY,
    DAC_FILTER_SLOW_PHASE_COMPENSATED,
    DAC_FILTER_NON_OVERSAMPLING
} DacFilter;

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

typedef enum UsbConnect {
    USB_CONNECT_ASK,
    USB_CONNECT_STORAGE,
    USB_CONNECT_CHARGE
} UsbConnect;

typedef enum AccentColor {
    ACCENT_BLUE,
    ACCENT_RED,
    ACCENT_ORANGE,
    ACCENT_GREEN,
    ACCENT_TEAL,
    ACCENT_PURPLE,
    ACCENT_PINK,
    ACCENT_GRAPHITE
} AccentColor;

typedef enum AppearanceTheme {
    APPEARANCE_GRAPHITE,
    APPEARANCE_NEON_NOIR,
    APPEARANCE_SUNSET,
    APPEARANCE_SEA_GLASS,
    APPEARANCE_CLASSIC
} AppearanceTheme;

typedef enum ButtonAction {
    BUTTON_ACTION_DEFAULT,
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

typedef enum EqualizerPreset {
    EQUALIZER_OFF,
    EQUALIZER_RETRO,
    EQUALIZER_BASS,
    EQUALIZER_HEAVY,
    EQUALIZER_POP,
    EQUALIZER_JAZZ,
    EQUALIZER_UNIQUE,
    EQUALIZER_CUSTOM,
    EQUALIZER_PARAMETRIC
} EqualizerPreset;

typedef enum TextSize {
    TEXT_SIZE_SMALL,
    TEXT_SIZE_MEDIUM,
    TEXT_SIZE_LARGE
} TextSize;

typedef enum SkipGesture {
    SKIP_GESTURE_HOLD,
    SKIP_GESTURE_DOUBLE_VOLUME,
    SKIP_GESTURE_PLAY_CLICKS
} SkipGesture;

typedef enum Orientation {
    ORIENTATION_LANDSCAPE,
    ORIENTATION_PORTRAIT,
    ORIENTATION_PORTRAIT_FLIPPED
} Orientation;

typedef enum TrackTransition {
    TRACK_TRANSITION_OFF,
    TRACK_TRANSITION_FADE,
    TRACK_TRANSITION_CROSSFADE
} TrackTransition;

enum {
    GAIN_LEVEL_COUNT = GAIN_HIGH + 1,
    DAC_FILTER_COUNT = DAC_FILTER_NON_OVERSAMPLING + 1,
    APPEARANCE_THEME_COUNT = APPEARANCE_CLASSIC + 1,
    BUTTON_ACTION_COUNT = BUTTON_ACTION_HOME + 1,
    PLAYBACK_BUTTON_COUNT = PLAYBACK_MENU_HOLD + 1,
    SETTINGS_BUTTONS_FILE_SIZE = 32,
    OUTPUT_JACK_COUNT = OUTPUT_BALANCED + 1,
    START_SCREEN_COUNT = START_COVER_FLOW + 1,
    USB_MODE_COUNT = USB_MODE_DAC + 1,
    USB_CONNECT_COUNT = USB_CONNECT_CHARGE + 1,
    ACCENT_COLOR_COUNT = ACCENT_GRAPHITE + 1,
    EQUALIZER_PRESET_COUNT = EQUALIZER_PARAMETRIC + 1,
    TEXT_SIZE_COUNT = TEXT_SIZE_LARGE + 1,
    SKIP_GESTURE_COUNT = SKIP_GESTURE_PLAY_CLICKS + 1,
    ORIENTATION_COUNT = ORIENTATION_PORTRAIT_FLIPPED + 1,
    TRACK_TRANSITION_COUNT = TRACK_TRANSITION_CROSSFADE + 1,
    SETTINGS_EQUALIZER_BANDS = 10,
    SETTINGS_EQUALIZER_GAIN_MAX = 20,
    SETTINGS_UNITY_PRE_GAIN = 32768,
    SETTINGS_BRIGHTNESS_MIN = 1,
    SETTINGS_BRIGHTNESS_MAX = 10,
    SETTINGS_VOLUME_MAX = 60,
    SETTINGS_DEFAULT_DAC_VOLUME = 35,
    SCREEN_TIMEOUT_OPTION_COUNT = 7,
    SLEEP_TIMER_OPTION_COUNT = 7,
    AUTO_OFF_OPTION_COUNT = 4,
    SETTINGS_DEFAULT_SAFE_START_VOLUME = 24,
    SETTINGS_TRANSITION_TENTHS_MIN = 5,
    SETTINGS_TRANSITION_TENTHS_MAX = 60,
    SETTINGS_DEFAULT_TRANSITION_TENTHS = 15,
    SETTINGS_HOME_ITEM_COUNT = 10,
    SETTINGS_HOME_NOW_PLAYING_ITEM = 4,
    SETTINGS_HOME_SETTINGS_ITEM = 3,
    SETTINGS_HOME_FIRST_ITEM_COUNT = 9,
    SETTINGS_HOME_ORDER_SLOTS = 10,
    SETTINGS_HOME_PACKED_SIZE = 7,
    SETTINGS_FILE_SIZE = 78,
    SETTINGS_V15_FILE_SIZE = 58,
    SETTINGS_V14_FILE_SIZE = 42,
    SETTINGS_LANGUAGE_LENGTH = 5,
    SETTINGS_LANGUAGE_CAPACITY = SETTINGS_LANGUAGE_LENGTH + 1,
    SETTINGS_PREVIOUS_FILE_SIZE = 32
};

enum {
    SETTINGS_STATUS_VOLUME = 1,
    SETTINGS_STATUS_REPEAT = 2,
    SETTINGS_STATUS_POSITION = 4,
    SETTINGS_STATUS_SEGMENTED = 8
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
    UsbConnect usb_connect;
    uint8_t dac_volume;
    AccentColor accent;
    AppearanceTheme appearance;
    uint8_t button_actions[PLAYBACK_BUTTON_COUNT];
    EqualizerPreset equalizer;
    int8_t equalizer_gains[SETTINGS_EQUALIZER_BANDS];
    ParametricEq parametric;
    TextSize text_size;
    SkipGesture skip_gesture;
    Orientation orientation;
    char language[SETTINGS_LANGUAGE_CAPACITY];
    uint8_t replay_gain_mode;
    bool prevent_clipping;
    bool battery_percent;
    uint8_t status_items;
    bool rewind_on_resume;
    bool safe_start_volume;
    uint8_t safe_start_volume_level;
    bool clock_24h;
    int16_t clock_offset_minutes;
    uint8_t power_on_flags;
    TrackTransition track_transition;
    uint8_t transition_tenths;
    uint8_t dac_filter;
    uint8_t home_order[SETTINGS_HOME_ITEM_COUNT];
    uint16_t home_hidden;
    bool status_bar_compact;
    bool standby_enabled;
    uint8_t standby_power_off_option;
    uint8_t long_press_action;
    uint8_t style_flags;
} Settings;

typedef struct SettingsEqualizerCurve {
    uint16_t band_hz[SETTINGS_EQUALIZER_BANDS];
    int8_t gain_half_db[SETTINGS_EQUALIZER_BANDS];
    uint8_t band_count;
    uint16_t pre_gain;
    bool is_parametric;
    ParametricEq parametric;
} SettingsEqualizerCurve;

extern const uint16_t SETTINGS_EQUALIZER_BAND_HZ[SETTINGS_EQUALIZER_BANDS];

Settings settings_defaults(void);
uint32_t settings_timeout_option_seconds(int option);
uint32_t settings_sleep_option_minutes(int option);
uint32_t settings_screen_timeout_ms(const Settings *settings);
uint32_t settings_auto_off_option_minutes(int option);
uint32_t settings_auto_off_ms(const Settings *settings);
uint8_t settings_safe_start_volume(const Settings *settings, uint8_t volume);
SettingsEqualizerCurve settings_equalizer_curve(const Settings *settings);
void settings_encode(const Settings *settings, uint8_t *bytes);
bool settings_decode(const uint8_t *bytes, uint32_t length, Settings *settings);
bool settings_home_unpack(const uint8_t *packed, int item_count, uint8_t *order, uint16_t *hidden);

ButtonAction settings_default_button_action(PlaybackButton button);
ButtonAction settings_button_action(const Settings *settings, PlaybackButton button);
void settings_buttons_encode(const Settings *settings, uint8_t *bytes);
bool settings_buttons_decode(const uint8_t *bytes, uint32_t length, Settings *settings);

NowPlayingStyle settings_now_playing_style(const Settings *settings);
void settings_set_now_playing_style(Settings *settings, NowPlayingStyle style);
