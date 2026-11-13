#include "settings/settings.h"

#include "core/format_chain.h"
#include "core/hash.h"
#include "equalizer_peak.h"

enum {
    VERSION = SETTINGS_FORMAT_VERSION,
    MAGIC_WORD = 0x54535344,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    BRIGHTNESS_FIELD = 6,
    TIMEOUT_FIELD = 7,
    GAIN_FIELD = 8,
    OUTPUT_FIELD = 9,
    START_FIELD = 10,
    SHUFFLE_FIELD = 11,
    REPEAT_FIELD = 12,
    VOLUME_FIELD = 13,
    AUTO_OFF_FIELD = 14,
    USB_MODE_FIELD = 15,
    DAC_VOLUME_FIELD = 16,
    ACCENT_FIELD = 17,
    EQUALIZER_FIELD = 18,
    LANGUAGE_FIELD = 19,
    HALF_DB_GAINS_FIELD = 28,
    TEXT_SIZE_FIELD = 24,
    SKIP_GESTURE_FIELD = 25,
    ORIENTATION_FIELD = 26,
    APPEARANCE_FIELD = 27,
    EXTENSION_FIELD = 38,
    EXTENSION_CHECKSUM_FIELD = 54,
    COMFORT_BYTE = 0,
    SAFE_LEVEL_BYTE = 1,
    TRANSITION_BYTE = 2,
    SPEED_BYTE = 3,
    HOME_ORDER_BYTE = 4,
    HOME_HIDDEN_BYTE = 9,
    CASSETTE_BIT = 1 << 5,
    STYLE_FLAGS_BYTE = 14,
    STYLE_HIGH_BIT = 1 << 7,
    CLOCK_OFFSET_BYTE = 11,
    STATUS_ITEMS_BYTE = 13,
    RESERVED_BYTE = 14,
    RESERVED_COUNT = 2,
    DAC_FILTER_BYTE = 14,
    DAC_FILTER_MASK = 0x07,
    SPOKEN_BYTE = 13,
    SKIP_SILENCE_BIT = 0x40,
    SPEECH_BOOST_BIT = 0x80,
    CROSSFEED_BYTE = 14,
    CROSSFEED_SHIFT = 5,
    CROSSFEED_MASK = 3,
    CROSSFEED_BITS = CROSSFEED_MASK << CROSSFEED_SHIFT,
    BATTERY_CARE_BYTE = 14,
    BATTERY_CARE_SHIFT = 3,
    BATTERY_CARE_MASK = 24,
    EXTENSION_SIZE = 16,
    SECOND_FIELD = 58,
    SECOND_CHECKSUM_FIELD = 74,
    PANEL_BYTE = 0,
    PANEL_PLAIN_START = 0x10,
    PANEL_STANDBY_OPTION_MASK = 3,
    PANEL_RESERVED = 0xE0,
    LONG_PRESS_BYTE = 1,
    LONG_PRESS_MASK = 7,
    BLUETOOTH_VOLUME_BYTE = 2,
    SECOND_RESERVED_BYTE = 3,
    POWER_ON_BYTE = STATUS_ITEMS_BYTE,
    POWER_ON_BITS = 0x10,
    STATUS_ITEMS_BITS = 0xFF & ~(POWER_ON_BITS | SKIP_SILENCE_BIT | SPEECH_BOOST_BIT),
    REWIND_BIT = 1,
    SAFE_START_BIT = 2,
    CLOCK_24H_BIT = 4,
    TRANSITION_SHIFT = 3,
    HOME_HIDDEN_PACKED = HOME_HIDDEN_BYTE - HOME_ORDER_BYTE,
    NIBBLE_BITS = 4,
    NIBBLE_MASK = 0xF,
    CHECKSUM_SIZE = 4,
    CURVE_FREQUENCIES = 7,
    DEFAULT_BRIGHTNESS = 7,
    DEFAULT_TIMEOUT_OPTION = 2,
    DEFAULT_VOLUME = 13,
    DEFAULT_AUTO_OFF_OPTION = 2,
    MILLISECONDS_PER_MINUTE = 60000,
    MILLISECONDS_PER_SECOND = 1000
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'D', 'S', 'S', 'T'};
static const FormatChain CHAIN = {.current = VERSION, .steps = NULL};
static const uint8_t DEFAULT_EXTENSION[EXTENSION_SIZE] = {
    [COMFORT_BYTE] =
        REWIND_BIT | SAFE_START_BIT | CLOCK_24H_BIT | TRACK_TRANSITION_OFF << TRANSITION_SHIFT,
    [SAFE_LEVEL_BYTE] = SETTINGS_DEFAULT_SAFE_START_VOLUME,
    [TRANSITION_BYTE] = SETTINGS_DEFAULT_TRANSITION_TENTHS,
    [HOME_ORDER_BYTE] = 0x60,
    0x97,
    0x18,
    0x52,
    0x43,
    [HOME_HIDDEN_BYTE] = (1u << 1) | (1u << 2) | (1u << 5)};
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

static int16_t clock_offset(const uint8_t *extension)
{
    uint16_t offset =
        extension[CLOCK_OFFSET_BYTE] | (uint16_t)((uint16_t)extension[CLOCK_OFFSET_BYTE + 1] << 8);
    return (int16_t)(offset <= INT16_MAX ? offset : (int32_t)offset - 65536);
}

static void read_extension(const uint8_t *extension, Settings *settings)
{
    settings->status_items = extension[STATUS_ITEMS_BYTE];
    settings->battery_care =
        (uint8_t)((extension[BATTERY_CARE_BYTE] & BATTERY_CARE_MASK) >> BATTERY_CARE_SHIFT);
    uint8_t comfort = extension[COMFORT_BYTE];
    settings->rewind_on_resume = (comfort & REWIND_BIT) != 0;
    settings->safe_start_volume = (comfort & SAFE_START_BIT) != 0;
    settings->clock_24h = (comfort & CLOCK_24H_BIT) != 0;
    settings->clock_offset_minutes = clock_offset(extension);
    settings->power_on_flags = extension[POWER_ON_BYTE];
    settings->track_transition = (TrackTransition)(comfort >> TRANSITION_SHIFT);
    settings->safe_start_volume_level = extension[SAFE_LEVEL_BYTE];
    settings->transition_tenths = extension[TRANSITION_BYTE];
    settings->playback_speed = (uint8_t)(SETTINGS_SPEED_NORMAL + (int8_t)extension[SPEED_BYTE]);
    settings->style_flags = extension[STYLE_FLAGS_BYTE] & STYLE_HIGH_BIT;
    settings_home_unpack(extension + HOME_ORDER_BYTE, SETTINGS_HOME_ITEM_COUNT,
                         settings->home_order, &settings->home_hidden);
    settings->dac_filter = extension[DAC_FILTER_BYTE] & DAC_FILTER_MASK;
    settings->skip_silence = (extension[SPOKEN_BYTE] & SKIP_SILENCE_BIT) != 0;
    settings->speech_boost = (extension[SPOKEN_BYTE] & SPEECH_BOOST_BIT) != 0;
    settings->crossfeed = (extension[CROSSFEED_BYTE] >> CROSSFEED_SHIFT) & CROSSFEED_MASK;
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
        .skip_gesture = SKIP_GESTURE_HOLD,
        .orientation = ORIENTATION_LANDSCAPE,
        .replay_gain_mode = REPLAY_GAIN_OFF,
        .prevent_clipping = true,
    };
    settings.standby_power_off_option = 1;
    settings.startup_animation = true;
    read_extension(DEFAULT_EXTENSION, &settings);
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

static uint32_t checksum_of(const uint8_t *bytes, int field)
{
    return hash_update(HASH_START, bytes, (uint32_t)field);
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
    bytes[SKIP_GESTURE_FIELD] = (uint8_t)settings->skip_gesture;
    bytes[ORIENTATION_FIELD] = (uint8_t)settings->orientation;
    bytes[APPEARANCE_FIELD] = (uint8_t)settings->appearance;
}

void settings_encode(const Settings *settings, uint8_t *bytes)
{
    for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
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
    hash_store(bytes + EXTENSION_CHECKSUM_FIELD, checksum_of(bytes, EXTENSION_CHECKSUM_FIELD));
    encode_second(settings, bytes + SECOND_FIELD);
    hash_store(bytes + SECOND_CHECKSUM_FIELD,
               hash_update(HASH_START, bytes + SECOND_FIELD, EXTENSION_SIZE));
}

static bool magic_valid(const uint8_t *bytes)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    return true;
}

bool settings_file_newer(const uint8_t *bytes, uint32_t length)
{
    return length > VERSION_FIELD && hash_load(bytes) == MAGIC_WORD &&
           bytes[VERSION_FIELD] > VERSION;
}

static bool header_valid(const uint8_t *bytes)
{
    if (!magic_valid(bytes)) {
        return false;
    }
    uint8_t flags = bytes[VERSION_FIELD + 1];
    return bytes[VERSION_FIELD] == VERSION && (flags & 0xC0u) == 0 &&
           (flags & 3u) < REPLAY_GAIN_MODE_COUNT && (flags >> 4u & 3u) < USB_CONNECT_COUNT &&
           hash_load(bytes + EXTENSION_CHECKSUM_FIELD) ==
               checksum_of(bytes, EXTENSION_CHECKSUM_FIELD) &&
           hash_load(bytes + SECOND_CHECKSUM_FIELD) ==
               hash_update(HASH_START, bytes + SECOND_FIELD, EXTENSION_SIZE);
}

static bool gains_valid(const uint8_t *bytes)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        if (bytes[HALF_DB_GAINS_FIELD + band] > 2 * SETTINGS_EQUALIZER_GAIN_MAX) {
            return false;
        }
    }
    return true;
}

static bool is_code_character(uint8_t byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '-';
}

static bool language_valid(const uint8_t *bytes)
{
    bool ended = false;
    for (int index = 0; index < SETTINGS_LANGUAGE_LENGTH; index++) {
        uint8_t byte = bytes[LANGUAGE_FIELD + index];
        if (byte != 0 && (ended || !is_code_character(byte))) {
            return false;
        }
        ended = byte == 0;
    }
    return true;
}

static bool home_valid(const uint8_t *extension)
{
    uint8_t order[SETTINGS_HOME_ITEM_COUNT];
    uint16_t hidden;
    return settings_home_unpack(extension + HOME_ORDER_BYTE, SETTINGS_HOME_ITEM_COUNT, order,
                                &hidden);
}

static bool reserved_clear(const uint8_t *extension)
{
    uint8_t any = 0;
    for (int index = RESERVED_BYTE; index < RESERVED_BYTE + RESERVED_COUNT; index++) {
        uint8_t reserved =
            index == DAC_FILTER_BYTE
                ? (uint8_t)~(DAC_FILTER_MASK | CROSSFEED_BITS | STYLE_HIGH_BIT | BATTERY_CARE_MASK)
                : 0xFF;
        any |= extension[index] & reserved;
    }
    return any == 0 && (extension[DAC_FILTER_BYTE] & DAC_FILTER_MASK) < DAC_FILTER_COUNT;
}

static bool extension_valid(const uint8_t *extension)
{
    uint8_t comfort = extension[COMFORT_BYTE];
    uint8_t tenths = extension[TRANSITION_BYTE];
    int16_t offset = clock_offset(extension);
    uint8_t speed = (uint8_t)(extension[SPEED_BYTE] + SETTINGS_SPEED_NORMAL);
    return comfort >> TRANSITION_SHIFT < TRACK_TRANSITION_COUNT && speed < SETTINGS_SPEED_COUNT &&
           extension[SAFE_LEVEL_BYTE] <= SETTINGS_VOLUME_MAX &&
           tenths >= SETTINGS_TRANSITION_TENTHS_MIN && tenths <= SETTINGS_TRANSITION_TENTHS_MAX &&
           offset >= -720 && offset <= 840 && home_valid(extension) && reserved_clear(extension) &&
           ((extension[BATTERY_CARE_BYTE] & BATTERY_CARE_MASK) >> BATTERY_CARE_SHIFT) < 3;
}

static bool second_valid(const uint8_t *second)
{
    uint8_t reserved = 0;
    for (int index = SECOND_RESERVED_BYTE; index < EXTENSION_SIZE; index++) {
        reserved |= second[index];
    }
    return reserved == 0 && (second[PANEL_BYTE] & PANEL_RESERVED) == 0 &&
           (second[PANEL_BYTE] >> 2 & PANEL_STANDBY_OPTION_MASK) < 3 &&
           (second[LONG_PRESS_BYTE] & ~LONG_PRESS_MASK) == 0 &&
           second[BLUETOOTH_VOLUME_BYTE] <= SETTINGS_VOLUME_MAX;
}

static bool values_valid(const uint8_t *bytes)
{
    return bytes[BRIGHTNESS_FIELD] >= SETTINGS_BRIGHTNESS_MIN &&
           bytes[BRIGHTNESS_FIELD] <= SETTINGS_BRIGHTNESS_MAX &&
           bytes[TIMEOUT_FIELD] < SCREEN_TIMEOUT_OPTION_COUNT &&
           bytes[GAIN_FIELD] < GAIN_LEVEL_COUNT && bytes[OUTPUT_FIELD] < OUTPUT_JACK_COUNT &&
           bytes[START_FIELD] < START_SCREEN_COUNT && bytes[SHUFFLE_FIELD] < SHUFFLE_MODE_COUNT &&
           bytes[REPEAT_FIELD] < REPEAT_MODE_COUNT && bytes[VOLUME_FIELD] <= SETTINGS_VOLUME_MAX &&
           bytes[AUTO_OFF_FIELD] < AUTO_OFF_OPTION_COUNT &&
           bytes[USB_MODE_FIELD] < USB_MODE_COUNT &&
           bytes[DAC_VOLUME_FIELD] <= SETTINGS_VOLUME_MAX &&
           bytes[ACCENT_FIELD] < ACCENT_COLOR_COUNT &&
           bytes[EQUALIZER_FIELD] < EQUALIZER_PRESET_COUNT &&
           bytes[TEXT_SIZE_FIELD] < TEXT_SIZE_COUNT &&
           bytes[SKIP_GESTURE_FIELD] < SKIP_GESTURE_COUNT &&
           bytes[ORIENTATION_FIELD] < ORIENTATION_COUNT &&
           bytes[APPEARANCE_FIELD] < APPEARANCE_THEME_COUNT;
}

static void decode_values(const uint8_t *bytes, Settings *settings)
{
    uint8_t flags = bytes[VERSION_FIELD + 1];
    *settings = (Settings){
        .brightness = bytes[BRIGHTNESS_FIELD],
        .screen_timeout = bytes[TIMEOUT_FIELD],
        .gain = (GainLevel)bytes[GAIN_FIELD],
        .output = (OutputJack)bytes[OUTPUT_FIELD],
        .start_screen = (StartScreen)bytes[START_FIELD],
        .shuffle = (ShuffleMode)bytes[SHUFFLE_FIELD],
        .repeat = (RepeatMode)bytes[REPEAT_FIELD],
        .volume = bytes[VOLUME_FIELD],
        .auto_off = bytes[AUTO_OFF_FIELD],
        .usb_mode = (UsbMode)bytes[USB_MODE_FIELD],
        .dac_volume = bytes[DAC_VOLUME_FIELD],
        .accent = (AccentColor)bytes[ACCENT_FIELD],
        .equalizer = (EqualizerPreset)bytes[EQUALIZER_FIELD],
        .text_size = (TextSize)bytes[TEXT_SIZE_FIELD],
        .skip_gesture = (SkipGesture)bytes[SKIP_GESTURE_FIELD],
        .orientation = (Orientation)bytes[ORIENTATION_FIELD],
        .appearance = (AppearanceTheme)bytes[APPEARANCE_FIELD],
        .replay_gain_mode = (uint8_t)(flags & 3u),
        .prevent_clipping = (flags & 4u) != 0,
        .battery_percent = (flags & 8u) != 0,
        .usb_connect = (UsbConnect)(flags >> 4u & 3u),
    };
}

static void read_second(const uint8_t *second, Settings *settings)
{
    settings->status_bar_compact = (second[PANEL_BYTE] & 1u) != 0;
    settings->standby_enabled = (second[PANEL_BYTE] & 2u) != 0;
    settings->standby_power_off_option = second[PANEL_BYTE] >> 2 & PANEL_STANDBY_OPTION_MASK;
    settings->startup_animation = (second[PANEL_BYTE] & PANEL_PLAIN_START) == 0;
    settings->long_press_action = second[LONG_PRESS_BYTE];
    settings->bluetooth_volume = second[BLUETOOTH_VOLUME_BYTE];
}

static bool decode_current(const uint8_t *bytes, uint32_t length, Settings *settings);

static bool decode_older(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    uint8_t upgraded[SETTINGS_FILE_CAPACITY];
    if (length > sizeof upgraded) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        upgraded[index] = bytes[index];
    }
    FormatBytes file = {.bytes = upgraded, .length = length, .capacity = sizeof upgraded};
    return format_upgrade(&CHAIN, bytes[VERSION_FIELD], &file) &&
           decode_current(upgraded, file.length, settings);
}

bool settings_decode(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (VERSION > 1 && length > VERSION_FIELD && magic_valid(bytes) && bytes[VERSION_FIELD] != 0 &&
        bytes[VERSION_FIELD] < VERSION) {
        return decode_older(bytes, length, settings);
    }
    return decode_current(bytes, length, settings);
}

static bool decode_current(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (length < SETTINGS_FILE_SIZE || !header_valid(bytes) || !values_valid(bytes) ||
        !gains_valid(bytes) || !language_valid(bytes) ||
        !extension_valid(bytes + EXTENSION_FIELD) || !second_valid(bytes + SECOND_FIELD)) {
        return false;
    }
    decode_values(bytes, settings);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        settings->equalizer_gains[band] =
            (int8_t)(bytes[HALF_DB_GAINS_FIELD + band] - SETTINGS_EQUALIZER_GAIN_MAX);
    }
    for (int index = 0; index < SETTINGS_LANGUAGE_LENGTH; index++) {
        settings->language[index] = (char)bytes[LANGUAGE_FIELD + index];
    }
    read_extension(bytes + EXTENSION_FIELD, settings);
    read_second(bytes + SECOND_FIELD, settings);
    return true;
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
    hash_store(bytes + 28, hash_update(HASH_START, bytes, 28));
}

bool settings_buttons_decode(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (length != SETTINGS_BUTTONS_FILE_SIZE || bytes[0] != 'D' || bytes[1] != 'S' ||
        bytes[2] != 'B' || bytes[3] != 'T' || bytes[4] != 1 || bytes[5] != PLAYBACK_BUTTON_COUNT ||
        hash_load(bytes + 28) != hash_update(HASH_START, bytes, 28)) {
        return false;
    }
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        if (bytes[8 + index] >= BUTTON_ACTION_COUNT) {
            return false;
        }
    }
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        settings->button_actions[index] = bytes[8 + index];
    }
    return true;
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
