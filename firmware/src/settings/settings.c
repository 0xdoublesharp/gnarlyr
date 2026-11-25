#include "settings/settings.h"

#include "settings_format.h"

#include "core/hash.h"
#include "core/record.h"
#include "equalizer_peak.h"

static const uint8_t DEFAULT_EXTENSION[EXTENSION_SIZE] = {
    [COMFORT_BYTE] =
        REWIND_BIT | SAFE_START_BIT | CLOCK_24H_BIT | TRACK_TRANSITION_OFF << TRANSITION_SHIFT,
    [SAFE_LEVEL_BYTE] = SETTINGS_DEFAULT_SAFE_START_VOLUME,
    [TRANSITION_BYTE] = SETTINGS_DEFAULT_TRANSITION_TENTHS,
    [HOME_ORDER_BYTE] = 0x60,
    0x75,
    0x98,
    0x21,
    0x43,
    [HOME_HIDDEN_BYTE] = (1u << 1) | (1u << 2)};
static const uint16_t TIMEOUT_SECONDS[SCREEN_TIMEOUT_OPTION_COUNT] = {10, 20, 30, 60, 120, 300, 0};
static const uint8_t SLEEP_MINUTES[SLEEP_TIMER_OPTION_COUNT] = {0, 15, 30, 45, 60, 90, 120};
static const uint8_t AUTO_OFF_MINUTES[AUTO_OFF_OPTION_COUNT] = {0, 1, 5, 10};
const uint16_t SETTINGS_EQUALIZER_BAND_HZ[SETTINGS_EQUALIZER_BANDS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static const uint16_t CURVE_HZ[CURVE_FREQUENCIES] = {63, 100, 125, 315, 1000, 3150, 10000};
static const int8_t PRESET_GAINS[EQUALIZER_PRESET_COUNT][CURVE_FREQUENCIES] = {
    [EQUALIZER_RETRO] = {0, 1, 0, 1, 1, 0, -6}, [EQUALIZER_BASS] = {5, 0, 3, 0, 0, 0, 0},
    [EQUALIZER_HEAVY] = {0, 0, 0, 3, 3, -3, 0}, [EQUALIZER_POP] = {0, 4, 0, 0, 0, -3, 3},
    [EQUALIZER_JAZZ] = {0, 4, 0, -6, 4, 1, 6},  [EQUALIZER_UNIQUE] = {0, 3, 0, 0, -3, 3, 0}};
static const uint16_t PRESET_PRE_GAINS[EQUALIZER_PRESET_COUNT] = {
    SETTINGS_UNITY_PRE_GAIN, 21504, 14336, 20480, 20480, 14336, 14336, 12288};

static uint8_t home_item(const uint8_t *packed, int index)
{
    return (packed[index / 2] >> (NIBBLE_BITS * (index & 1))) & NIBBLE_MASK;
}

static int home_stored_count(const uint8_t *packed)
{
    unsigned seen = 0;
    int count = 0;
    for (int index = 0; index < SETTINGS_HOME_ORDER_SLOTS; index++) {
        seen |= 1u << home_item(packed, index);
        count = seen == (2u << index) - 1 ? index + 1 : count;
    }
    for (int index = count; index < SETTINGS_HOME_ORDER_SLOTS; index++) {
        if (home_item(packed, index) != 0) {
            return 0;
        }
    }
    return count;
}

bool settings_home_unpack(const uint8_t *packed, int item_count, uint8_t *order, uint16_t *hidden)
{
    int stored = home_stored_count(packed);
    unsigned mask = packed[HOME_HIDDEN_PACKED] | (unsigned)packed[HOME_HIDDEN_PACKED + 1] << 8;
    unsigned hideable = ((1u << stored) - 1) & ~(1u << SETTINGS_HOME_SETTINGS_ITEM);
    if (stored < SETTINGS_HOME_FIRST_ITEM_COUNT || (mask & ~hideable) != 0) {
        return false;
    }
    int count = 0;
    for (int index = 0; index < stored; index++) {
        uint8_t item = home_item(packed, index);
        if (item < item_count) {
            order[count++] = item;
        }
    }
    for (; count < item_count; count++) {
        order[count] = (uint8_t)count;
    }
    *hidden = (uint16_t)(mask & ((1u << item_count) - 1));
    return true;
}

Settings settings_defaults(void)
{
    Settings settings = {
        .brightness = DEFAULT_BRIGHTNESS,
        .screen_timeout = DEFAULT_TIMEOUT_OPTION,
        .gain = GAIN_LOW,
        .output = OUTPUT_AUTO,
        .start_screen = START_MAIN_MENU,
        .shuffle = SHUFFLE_OFF,
        .repeat = REPEAT_OFF,
        .volume = DEFAULT_VOLUME,
        .auto_off = DEFAULT_AUTO_OFF_OPTION,
        .usb_mode = USB_MODE_STORAGE,
        .dac_volume = SETTINGS_DEFAULT_DAC_VOLUME,
        .bluetooth_volume = SETTINGS_DEFAULT_BLUETOOTH_VOLUME,
        .accent = ACCENT_BLUE,
        .appearance = APPEARANCE_NEON_NOIR,
        .equalizer = EQUALIZER_OFF,
        .text_size = TEXT_SIZE_MEDIUM,
        .orientation = ORIENTATION_LANDSCAPE,
        .replay_gain_mode = REPLAY_GAIN_OFF,
        .prevent_clipping = true,
    };
    settings.standby_power_off_option = 1;
    settings.startup_animation = true;
    settings_read_extension(DEFAULT_EXTENSION, &settings);
    return settings;
}

uint32_t settings_timeout_option_seconds(int option)
{
    return option >= 0 && option < SCREEN_TIMEOUT_OPTION_COUNT ? TIMEOUT_SECONDS[option] : 0;
}

uint32_t settings_sleep_option_minutes(int option)
{
    return option >= 0 && option < SLEEP_TIMER_OPTION_COUNT ? SLEEP_MINUTES[option] : 0;
}

uint32_t settings_screen_timeout_ms(const Settings *settings)
{
    return settings_timeout_option_seconds(settings->screen_timeout) * MILLISECONDS_PER_SECOND;
}

uint32_t settings_auto_off_option_minutes(int option)
{
    return option >= 0 && option < AUTO_OFF_OPTION_COUNT ? AUTO_OFF_MINUTES[option] : 0;
}

uint32_t settings_auto_off_ms(const Settings *settings)
{
    return settings_auto_off_option_minutes(settings->auto_off) * MILLISECONDS_PER_MINUTE;
}

uint8_t settings_safe_start_volume(const Settings *settings, uint8_t volume)
{
    bool capped = settings->safe_start_volume && volume > settings->safe_start_volume_level;
    return capped ? settings->safe_start_volume_level : volume;
}

uint8_t settings_safe_start_bluetooth_volume(const Settings *settings, uint8_t volume)
{
    bool capped = settings->safe_start_volume && volume > SETTINGS_BLUETOOTH_SAFE_START_VOLUME;
    return capped ? (uint8_t)SETTINGS_BLUETOOTH_SAFE_START_VOLUME : volume;
}

static void add_band(SettingsEqualizerCurve *curve, uint16_t hz, int gain)
{
    if (gain != 0) {
        curve->band_hz[curve->band_count] = hz;
        curve->gain_half_db[curve->band_count++] = (int8_t)gain;
    }
}

static bool flat(const int8_t *gains)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        if (gains[band] != 0) {
            return false;
        }
    }
    return true;
}

static void custom_curve(const Settings *settings, SettingsEqualizerCurve *curve)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        add_band(curve, SETTINGS_EQUALIZER_BAND_HZ[band], settings->equalizer_gains[band]);
    }
    curve->pre_gain = flat(settings->equalizer_gains)
                          ? SETTINGS_UNITY_PRE_GAIN
                          : equalizer_peak_pre_gain(settings->equalizer_gains);
}

SettingsEqualizerCurve settings_equalizer_curve(const Settings *settings)
{
    SettingsEqualizerCurve curve = {0};
    if (settings->equalizer == EQUALIZER_PARAMETRIC) {
        curve.is_parametric = true;
        curve.parametric = settings->parametric;
        curve.pre_gain = SETTINGS_UNITY_PRE_GAIN;
        return curve;
    }
    if (settings->equalizer == EQUALIZER_CUSTOM) {
        custom_curve(settings, &curve);
        return curve;
    }
    for (int index = 0; index < CURVE_FREQUENCIES; index++) {
        add_band(&curve, CURVE_HZ[index], 2 * PRESET_GAINS[settings->equalizer][index]);
    }
    curve.pre_gain = PRESET_PRE_GAINS[settings->equalizer];
    return curve;
}

static void encode_extension(const Settings *settings, uint8_t *extension)
{
    extension[COMFORT_BYTE] = (uint8_t)((settings->rewind_on_resume ? REWIND_BIT : 0) |
                                        (settings->safe_start_volume ? SAFE_START_BIT : 0) |
                                        (settings->clock_24h ? CLOCK_24H_BIT : 0) |
                                        ((unsigned)settings->track_transition << TRANSITION_SHIFT));
    extension[STATUS_ITEMS_BYTE] = (uint8_t)((settings->status_items & STATUS_ITEMS_BITS) |
                                             (settings->power_on_flags & POWER_ON_BITS));
    extension[SAFE_LEVEL_BYTE] = settings->safe_start_volume_level;
    extension[TRANSITION_BYTE] = settings->transition_tenths;
    extension[SPEED_BYTE] = (uint8_t)(settings->playback_speed - SETTINGS_SPEED_NORMAL);
    extension[CLOCK_OFFSET_BYTE] = (uint8_t)settings->clock_offset_minutes;
    extension[CLOCK_OFFSET_BYTE + 1] = (uint8_t)((uint16_t)settings->clock_offset_minutes >> 8);
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        extension[HOME_ORDER_BYTE + index / 2] |=
            (uint8_t)(settings->home_order[index] << (NIBBLE_BITS * (index & 1)));
    }
    extension[HOME_HIDDEN_BYTE] = (uint8_t)settings->home_hidden;
    extension[HOME_HIDDEN_BYTE + 1] = (uint8_t)(settings->home_hidden >> 8);
    extension[DAC_FILTER_BYTE] |= (uint8_t)(settings->dac_filter & DAC_FILTER_MASK);
    extension[SPOKEN_BYTE] |= (uint8_t)((settings->skip_silence ? SKIP_SILENCE_BIT : 0) |
                                        (settings->speech_boost ? SPEECH_BOOST_BIT : 0));
    extension[CROSSFEED_BYTE] |=
        (uint8_t)((settings->crossfeed & CROSSFEED_MASK) << CROSSFEED_SHIFT);
    extension[STYLE_FLAGS_BYTE] |= (uint8_t)(settings->style_flags & STYLE_HIGH_BIT);
    extension[BATTERY_CARE_BYTE] |=
        (uint8_t)((settings->battery_care << BATTERY_CARE_SHIFT) & BATTERY_CARE_MASK);
}

static void encode_second(const Settings *settings, uint8_t *second)
{
    second[PANEL_BYTE] = (uint8_t)(settings->status_bar_compact | settings->standby_enabled << 1 |
                                   settings->standby_power_off_option << 2 |
                                   (settings->startup_animation ? 0 : PANEL_PLAIN_START));
    second[LONG_PRESS_BYTE] = settings->long_press_action & LONG_PRESS_MASK;
    second[BLUETOOTH_VOLUME_BYTE] = settings->bluetooth_volume < SETTINGS_VOLUME_MAX
                                        ? settings->bluetooth_volume
                                        : (uint8_t)SETTINGS_VOLUME_MAX;
}

static void encode_values(const Settings *settings, uint8_t *bytes)
{
    bytes[VERSION_FIELD] = VERSION;
    bytes[VERSION_FIELD + 1] = settings->replay_gain_mode | (settings->prevent_clipping ? 4u : 0u) |
                               (settings->battery_percent ? 8u : 0u) |
                               (uint8_t)(settings->usb_connect << 4u);
    bytes[BRIGHTNESS_FIELD] = settings->brightness;
    bytes[TIMEOUT_FIELD] = settings->screen_timeout;
    bytes[GAIN_FIELD] = (uint8_t)settings->gain;
    bytes[OUTPUT_FIELD] = (uint8_t)settings->output;
    bytes[START_FIELD] = (uint8_t)settings->start_screen;
    bytes[SHUFFLE_FIELD] = (uint8_t)settings->shuffle;
    bytes[REPEAT_FIELD] = (uint8_t)settings->repeat;
    bytes[VOLUME_FIELD] = settings->volume;
    bytes[AUTO_OFF_FIELD] = settings->auto_off;
    bytes[USB_MODE_FIELD] = (uint8_t)settings->usb_mode;
    bytes[DAC_VOLUME_FIELD] = settings->dac_volume;
    bytes[ACCENT_FIELD] = (uint8_t)settings->accent;
    bytes[EQUALIZER_FIELD] = (uint8_t)settings->equalizer;
    bytes[TEXT_SIZE_FIELD] = (uint8_t)settings->text_size;
    bytes[ORIENTATION_FIELD] = (uint8_t)settings->orientation;
    bytes[APPEARANCE_FIELD] = (uint8_t)settings->appearance;
}

void settings_encode(const Settings *settings, uint8_t *bytes)
{
    for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? SETTINGS_MAGIC[index] : 0;
    }
    encode_values(settings, bytes);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        bytes[HALF_DB_GAINS_FIELD + band] =
            (uint8_t)(settings->equalizer_gains[band] + SETTINGS_EQUALIZER_GAIN_MAX);
    }
    for (int index = 0; index < SETTINGS_LANGUAGE_LENGTH && settings->language[index] != '\0';
         index++) {
        bytes[LANGUAGE_FIELD + index] = (uint8_t)settings->language[index];
    }
    encode_extension(settings, bytes + EXTENSION_FIELD);
    record_seal(bytes, EXTENSION_CHECKSUM_FIELD);
    encode_second(settings, bytes + SECOND_FIELD);
    record_seal(bytes + SECOND_FIELD, EXTENSION_SIZE);
}

bool settings_file_newer(const uint8_t *bytes, uint32_t length)
{
    return length > VERSION_FIELD && hash_load(bytes) == MAGIC_WORD &&
           bytes[VERSION_FIELD] > VERSION;
}

ButtonAction settings_default_button_action(PlaybackButton button)
{
    (void)button;
    return BUTTON_ACTION_DEFAULT;
}

ButtonAction settings_button_action(const Settings *settings, PlaybackButton button)
{
    if ((unsigned)button >= PLAYBACK_BUTTON_COUNT ||
        settings->button_actions[button] >= BUTTON_ACTION_COUNT) {
        return BUTTON_ACTION_DEFAULT;
    }
    return (ButtonAction)settings->button_actions[button];
}

void settings_buttons_encode(const Settings *settings, uint8_t *bytes)
{
    static const uint8_t magic[] = {'D', 'S', 'B', 'T'};
    for (int index = 0; index < SETTINGS_BUTTONS_FILE_SIZE; index++) {
        bytes[index] = index < 4 ? magic[index] : 0;
    }
    bytes[4] = 1;
    bytes[5] = PLAYBACK_BUTTON_COUNT;
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        bytes[8 + index] = (uint8_t)settings_button_action(settings, (PlaybackButton)index);
    }
    record_seal(bytes, 28);
}

uint16_t settings_speed_percent(int option)
{
    static const uint16_t PERCENT[SETTINGS_SPEED_COUNT] = {50,  75,  100, 125, 150,
                                                           175, 200, 250, 300};
    return option >= 0 && option < SETTINGS_SPEED_COUNT ? PERCENT[option]
                                                        : PERCENT[SETTINGS_SPEED_NORMAL];
}

const StringId SETTINGS_SPEED_LABELS[SETTINGS_SPEED_COUNT] = {
    STR_SPEED_050, STR_SPEED_075, STR_SPEED_100, STR_SPEED_125, STR_SPEED_150,
    STR_SPEED_175, STR_SPEED_200, STR_SPEED_250, STR_SPEED_300};

NowPlayingStyle settings_now_playing_style(const Settings *settings)
{
    return (NowPlayingStyle)(((settings->status_items & CASSETTE_BIT) >> 5) |
                             ((settings->style_flags & STYLE_HIGH_BIT) >> 6));
}

void settings_set_now_playing_style(Settings *settings, NowPlayingStyle style)
{
    unsigned value = (unsigned)style <= NOW_PLAYING_SPECTRUM ? (unsigned)style : 0;
    settings->status_items =
        (uint8_t)((settings->status_items & (uint8_t)~CASSETTE_BIT) | ((value & 1) << 5));
    settings->style_flags = (uint8_t)((value >> 1) << 7);
}
