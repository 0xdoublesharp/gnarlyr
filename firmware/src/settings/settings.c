#include "settings/settings.h"

#include "core/hash.h"
#include "equalizer_peak.h"
#include "settings/equalizer_fit.h"

enum {
    VERSION = 16,
    EXTENSION_VERSION = 15,
    REPLAY_GAIN_VERSION = 14,
    APPEARANCE_VERSION = 13,
    LANGUAGE_VERSION = 12,
    HALF_DB_VERSION = 11,
    ORIENTATION_VERSION = 10,
    VOLUME_SCALE_VERSION = 9,
    OLD_VOLUME_MAX = 50,
    VOLUME_SCALE_TENTHS = 12,
    VOLUME_SCALE_OFFSET_TENTHS = 5,
    TENTHS = 10,
    SKIP_GESTURE_VERSION = 8,
    TEXT_SIZE_VERSION = 7,
    EQUALIZER_VERSION = 6,
    ACCENT_VERSION = 5,
    FIRST_VERSION = 1,
    AUTO_OFF_VERSION = 2,
    USB_MODE_VERSION = 3,
    DAC_VOLUME_VERSION = 4,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    HEADER_SIZE = 6,
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
    EQUALIZER_GAINS_FIELD = 19,
    LANGUAGE_FIELD = 19,
    OLD_CHECKSUM_FIELD = 20,
    OLD_BANDS = 5,
    OLD_GAIN_MAX = 10,
    HALF_DB_GAINS_FIELD = 28,
    HALF_DB_CHECKSUM_FIELD = 38,
    PREVIOUS_CHECKSUM_FIELD = 28,
    TEXT_SIZE_FIELD = 24,
    SKIP_GESTURE_FIELD = 25,
    ORIENTATION_FIELD = 26,
    APPEARANCE_FIELD = 27,
    EXTENSION_FIELD = 38,
    EXTENSION_CHECKSUM_FIELD = 54,
    COMFORT_BYTE = 0,
    SAFE_LEVEL_BYTE = 1,
    TRANSITION_BYTE = 2,
    HOME_ORDER_BYTE = 4,
    HOME_HIDDEN_BYTE = 9,
    CLOCK_OFFSET_BYTE = 11,
    STATUS_ITEMS_BYTE = 13,
    RESERVED_BYTE = 14,
    RESERVED_COUNT = 1,
    EXTENSION_SIZE = 16,
    REWIND_BIT = 1,
    SAFE_START_BIT = 2,
    CLOCK_24H_BIT = 4,
    TRANSITION_SHIFT = 3,
    HOME_HIDDEN_PACKED = HOME_HIDDEN_BYTE - HOME_ORDER_BYTE,
    NIBBLE_BITS = 4,
    NIBBLE_MASK = 0xF,
    CHECKSUM_SIZE = 4,
    CURVE_FREQUENCIES = 7,
    HUNDRED = 100,
    DEFAULT_BRIGHTNESS = 7,
    DEFAULT_TIMEOUT_OPTION = 2,
    DEFAULT_VOLUME = 13,
    DEFAULT_AUTO_OFF_OPTION = 2,
    MILLISECONDS_PER_MINUTE = 60000,
    MILLISECONDS_PER_SECOND = 1000
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'D', 'S', 'S', 'T'};
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
static const uint16_t CURVE_HZ[CURVE_FREQUENCIES] = {63, 100, 125, 315, 1000, 3150, 10000};
static const uint16_t OLD_BAND_HZ[OLD_BANDS] = {100, 315, 1000, 3150, 10000};
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
    uint8_t comfort = extension[COMFORT_BYTE];
    settings->rewind_on_resume = (comfort & REWIND_BIT) != 0;
    settings->safe_start_volume = (comfort & SAFE_START_BIT) != 0;
    settings->clock_24h = (comfort & CLOCK_24H_BIT) != 0;
    settings->clock_offset_minutes = clock_offset(extension);
    settings->track_transition = (TrackTransition)(comfort >> TRANSITION_SHIFT);
    settings->safe_start_volume_level = extension[SAFE_LEVEL_BYTE];
    settings->transition_tenths = extension[TRANSITION_BYTE];
    settings_home_unpack(extension + HOME_ORDER_BYTE, SETTINGS_HOME_ITEM_COUNT,
                         settings->home_order, &settings->home_hidden);
    bool untouched = settings->home_hidden == 0;
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        untouched = untouched && settings->home_order[index] == index;
    }
    static const uint8_t PREVIOUS[] = {0, 6, 7, 8, 9, 1, 2, 5, 3, 4};
    bool previous = settings->home_hidden == 0x26;
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        previous = previous && settings->home_order[index] == PREVIOUS[index];
    }
    if (untouched || previous) {
        settings_home_unpack(DEFAULT_EXTENSION + HOME_ORDER_BYTE, SETTINGS_HOME_ITEM_COUNT,
                             settings->home_order, &settings->home_hidden);
    }
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

static int checksum_field(uint8_t version)
{
    return version < EQUALIZER_VERSION   ? OLD_CHECKSUM_FIELD
           : version < HALF_DB_VERSION   ? PREVIOUS_CHECKSUM_FIELD
           : version < EXTENSION_VERSION ? HALF_DB_CHECKSUM_FIELD
                                         : EXTENSION_CHECKSUM_FIELD;
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
    extension[STATUS_ITEMS_BYTE] = settings->status_items;
    extension[SAFE_LEVEL_BYTE] = settings->safe_start_volume_level;
    extension[TRANSITION_BYTE] = settings->transition_tenths;
    extension[CLOCK_OFFSET_BYTE] = (uint8_t)settings->clock_offset_minutes;
    extension[CLOCK_OFFSET_BYTE + 1] = (uint8_t)((uint16_t)settings->clock_offset_minutes >> 8);
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        extension[HOME_ORDER_BYTE + index / 2] |=
            (uint8_t)(settings->home_order[index] << (NIBBLE_BITS * (index & 1)));
    }
    extension[HOME_HIDDEN_BYTE] = (uint8_t)settings->home_hidden;
    extension[HOME_HIDDEN_BYTE + 1] = (uint8_t)(settings->home_hidden >> 8);
}

void settings_encode(const Settings *settings, uint8_t *bytes)
{
    for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
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
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        bytes[HALF_DB_GAINS_FIELD + band] =
            (uint8_t)(settings->equalizer_gains[band] + SETTINGS_EQUALIZER_GAIN_MAX);
    }
    bytes[TEXT_SIZE_FIELD] = (uint8_t)settings->text_size;
    bytes[SKIP_GESTURE_FIELD] = (uint8_t)settings->skip_gesture;
    bytes[ORIENTATION_FIELD] = (uint8_t)settings->orientation;
    bytes[APPEARANCE_FIELD] = (uint8_t)settings->appearance;
    for (int index = 0; index < SETTINGS_LANGUAGE_LENGTH && settings->language[index] != '\0';
         index++) {
        bytes[LANGUAGE_FIELD + index] = (uint8_t)settings->language[index];
    }
    encode_extension(settings, bytes + EXTENSION_FIELD);
    bytes[53] = 16;
    hash_store(bytes + EXTENSION_CHECKSUM_FIELD, checksum_of(bytes, EXTENSION_CHECKSUM_FIELD));
    bytes[58] = (uint8_t)(settings->status_bar_compact | settings->standby_enabled << 1 |
                          settings->standby_power_off_option << 2);
    bytes[59] = settings->long_press_action & 7u;
    bytes[60] = (uint8_t)((settings->bluetooth_volume < SETTINGS_VOLUME_MAX ? settings->bluetooth_volume
                                                                          : SETTINGS_VOLUME_MAX) +
                          1u);
    hash_store(bytes + 74, hash_update(HASH_START, bytes + 58, 16));
}

static bool replay_gain_flags_valid(const uint8_t *bytes)
{
    uint8_t flags = bytes[VERSION_FIELD + 1];
    return bytes[VERSION_FIELD] < REPLAY_GAIN_VERSION
               ? flags == 0
               : (flags & 0xC0u) == 0 && (flags & 3u) < REPLAY_GAIN_MODE_COUNT &&
                     (flags >> 4u & 3u) < USB_CONNECT_COUNT;
}

static bool header_valid(const uint8_t *bytes, uint32_t length)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    uint8_t version = bytes[VERSION_FIELD];
    int field = checksum_field(version);
    return version >= FIRST_VERSION && version <= VERSION && replay_gain_flags_valid(bytes) &&
           length >= (uint32_t)(field + CHECKSUM_SIZE) &&
           hash_load(bytes + field) == checksum_of(bytes, field);
}

static bool gains_valid(const uint8_t *bytes, int first, int count, int max)
{
    for (int band = 0; band < count; band++) {
        if (bytes[first + band] > 2 * max) {
            return false;
        }
    }
    return true;
}

static bool equalizer_valid(const uint8_t *bytes)
{
    uint8_t version = bytes[VERSION_FIELD];
    if (version < EQUALIZER_VERSION) {
        return true;
    }
    bool gains = version < HALF_DB_VERSION
                     ? gains_valid(bytes, EQUALIZER_GAINS_FIELD, OLD_BANDS, OLD_GAIN_MAX)
                     : gains_valid(bytes, HALF_DB_GAINS_FIELD, SETTINGS_EQUALIZER_BANDS,
                                   SETTINGS_EQUALIZER_GAIN_MAX);
    return gains && bytes[EQUALIZER_FIELD] < EQUALIZER_PRESET_COUNT;
}

static bool text_size_valid(const uint8_t *bytes)
{
    return bytes[VERSION_FIELD] < TEXT_SIZE_VERSION || bytes[TEXT_SIZE_FIELD] < TEXT_SIZE_COUNT;
}

static bool skip_gesture_valid(const uint8_t *bytes)
{
    return bytes[VERSION_FIELD] < SKIP_GESTURE_VERSION ||
           bytes[SKIP_GESTURE_FIELD] < SKIP_GESTURE_COUNT;
}

static bool orientation_valid(const uint8_t *bytes)
{
    return bytes[VERSION_FIELD] < ORIENTATION_VERSION ||
           bytes[ORIENTATION_FIELD] < ORIENTATION_COUNT;
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

static void decode_language(const uint8_t *bytes, Settings *settings)
{
    if (bytes[VERSION_FIELD] < LANGUAGE_VERSION) {
        return;
    }
    for (int index = 0; index < SETTINGS_LANGUAGE_LENGTH; index++) {
        settings->language[index] = (char)bytes[LANGUAGE_FIELD + index];
    }
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
    uint8_t any = extension[TRANSITION_BYTE + 1];
    for (int index = 0; index < RESERVED_COUNT; index++) {
        any |= extension[RESERVED_BYTE + index];
    }
    return any == 0;
}

static bool extension_valid(const uint8_t *bytes)
{
    if (bytes[VERSION_FIELD] < EXTENSION_VERSION) {
        return true;
    }
    const uint8_t *extension = bytes + EXTENSION_FIELD;
    uint8_t comfort = extension[COMFORT_BYTE];
    uint8_t tenths = extension[TRANSITION_BYTE];
    int16_t offset = clock_offset(extension);
    return comfort >> TRANSITION_SHIFT < TRACK_TRANSITION_COUNT &&
           extension[SAFE_LEVEL_BYTE] <= SETTINGS_VOLUME_MAX &&
           tenths >= SETTINGS_TRANSITION_TENTHS_MIN && tenths <= SETTINGS_TRANSITION_TENTHS_MAX &&
           offset >= -720 && offset <= 840 && home_valid(extension) && reserved_clear(extension) &&
           extension[15] == (bytes[VERSION_FIELD] >= 16 ? 16 : 0);
}

static void decode_extension(const uint8_t *bytes, Settings *settings)
{
    read_extension(bytes[VERSION_FIELD] < EXTENSION_VERSION ? DEFAULT_EXTENSION
                                                            : bytes + EXTENSION_FIELD,
                   settings);
}

static void migrate_gains(const uint8_t *bytes, int8_t *gains)
{
    EqualizerFit fit;
    equalizer_fit_start(&fit);
    for (int band = 0; band < OLD_BANDS; band++) {
        int gain = bytes[EQUALIZER_GAINS_FIELD + band] - OLD_GAIN_MAX;
        equalizer_fit_point(&fit, OLD_BAND_HZ[band], HUNDRED * gain);
    }
    equalizer_fit_finish(&fit, gains);
}

static void decode_equalizer(const uint8_t *bytes, Settings *settings)
{
    uint8_t version = bytes[VERSION_FIELD];
    settings->equalizer =
        version >= EQUALIZER_VERSION ? (EqualizerPreset)bytes[EQUALIZER_FIELD] : EQUALIZER_OFF;
    if (version >= HALF_DB_VERSION) {
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            settings->equalizer_gains[band] =
                (int8_t)(bytes[HALF_DB_GAINS_FIELD + band] - SETTINGS_EQUALIZER_GAIN_MAX);
        }
    } else if (version >= EQUALIZER_VERSION) {
        migrate_gains(bytes, settings->equalizer_gains);
    }
}

static uint8_t volume_max(const uint8_t *bytes)
{
    return bytes[VERSION_FIELD] < VOLUME_SCALE_VERSION ? OLD_VOLUME_MAX : SETTINGS_VOLUME_MAX;
}

static uint8_t saved_volume(const uint8_t *bytes, int field)
{
    uint32_t level = bytes[field];
    if (bytes[VERSION_FIELD] >= VOLUME_SCALE_VERSION || level == 0) {
        return (uint8_t)level;
    }
    level = (VOLUME_SCALE_TENTHS * level - VOLUME_SCALE_OFFSET_TENTHS) / TENTHS;
    return (uint8_t)(level > 0 ? level : 1);
}

static bool values_valid(const uint8_t *bytes)
{
    return bytes[BRIGHTNESS_FIELD] >= SETTINGS_BRIGHTNESS_MIN &&
           bytes[BRIGHTNESS_FIELD] <= SETTINGS_BRIGHTNESS_MAX &&
           bytes[TIMEOUT_FIELD] < SCREEN_TIMEOUT_OPTION_COUNT &&
           bytes[GAIN_FIELD] < GAIN_LEVEL_COUNT && bytes[OUTPUT_FIELD] < OUTPUT_JACK_COUNT &&
           bytes[START_FIELD] < START_SCREEN_COUNT && bytes[SHUFFLE_FIELD] < SHUFFLE_MODE_COUNT &&
           bytes[REPEAT_FIELD] < REPEAT_MODE_COUNT && bytes[VOLUME_FIELD] <= volume_max(bytes) &&
           bytes[AUTO_OFF_FIELD] < AUTO_OFF_OPTION_COUNT &&
           bytes[USB_MODE_FIELD] < USB_MODE_COUNT && bytes[DAC_VOLUME_FIELD] <= volume_max(bytes) &&
           bytes[ACCENT_FIELD] < ACCENT_COLOR_COUNT;
}

bool settings_decode(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (length < HEADER_SIZE || !header_valid(bytes, length) || !values_valid(bytes) ||
        !equalizer_valid(bytes) || !text_size_valid(bytes) || !skip_gesture_valid(bytes) ||
        !orientation_valid(bytes) || !extension_valid(bytes) ||
        (bytes[VERSION_FIELD] >= APPEARANCE_VERSION &&
         bytes[APPEARANCE_FIELD] >= APPEARANCE_THEME_COUNT) ||
        (bytes[VERSION_FIELD] >= LANGUAGE_VERSION && !language_valid(bytes))) {
        return false;
    }
    *settings = (Settings){
        .brightness = bytes[BRIGHTNESS_FIELD],
        .screen_timeout = bytes[TIMEOUT_FIELD],
        .gain = (GainLevel)bytes[GAIN_FIELD],
        .output = (OutputJack)bytes[OUTPUT_FIELD],
        .start_screen = (StartScreen)bytes[START_FIELD],
        .shuffle = (ShuffleMode)bytes[SHUFFLE_FIELD],
        .repeat = (RepeatMode)bytes[REPEAT_FIELD],
        .volume = saved_volume(bytes, VOLUME_FIELD),
        .auto_off = bytes[VERSION_FIELD] < AUTO_OFF_VERSION ? (uint8_t)DEFAULT_AUTO_OFF_OPTION
                                                            : bytes[AUTO_OFF_FIELD],
        .usb_mode = bytes[VERSION_FIELD] < USB_MODE_VERSION ? USB_MODE_STORAGE
                                                            : (UsbMode)bytes[USB_MODE_FIELD],
        .dac_volume = bytes[VERSION_FIELD] < DAC_VOLUME_VERSION
                          ? (uint8_t)SETTINGS_DEFAULT_DAC_VOLUME
                          : saved_volume(bytes, DAC_VOLUME_FIELD),
        .accent =
            bytes[VERSION_FIELD] < ACCENT_VERSION ? ACCENT_BLUE : (AccentColor)bytes[ACCENT_FIELD],
    };
    decode_equalizer(bytes, settings);
    settings->text_size = bytes[VERSION_FIELD] < TEXT_SIZE_VERSION
                              ? TEXT_SIZE_MEDIUM
                              : (TextSize)bytes[TEXT_SIZE_FIELD];
    settings->skip_gesture = bytes[VERSION_FIELD] < SKIP_GESTURE_VERSION
                                 ? SKIP_GESTURE_HOLD
                                 : (SkipGesture)bytes[SKIP_GESTURE_FIELD];
    settings->orientation = bytes[VERSION_FIELD] < ORIENTATION_VERSION
                                ? ORIENTATION_LANDSCAPE
                                : (Orientation)bytes[ORIENTATION_FIELD];
    settings->appearance = bytes[VERSION_FIELD] < APPEARANCE_VERSION
                               ? APPEARANCE_NEON_NOIR
                               : (AppearanceTheme)bytes[APPEARANCE_FIELD];
    decode_language(bytes, settings);
    settings->replay_gain_mode = bytes[VERSION_FIELD] < REPLAY_GAIN_VERSION
                                     ? REPLAY_GAIN_OFF
                                     : (uint8_t)(bytes[VERSION_FIELD + 1] & 3u);
    settings->prevent_clipping =
        bytes[VERSION_FIELD] < REPLAY_GAIN_VERSION || (bytes[VERSION_FIELD + 1] & 4u) != 0;
    settings->battery_percent = (bytes[VERSION_FIELD + 1] & 8u) != 0;
    settings->usb_connect = (UsbConnect)(bytes[VERSION_FIELD + 1] >> 4u & 3u);
    decode_extension(bytes, settings);
    settings->standby_power_off_option = 1;
    settings->bluetooth_volume = SETTINGS_DEFAULT_BLUETOOTH_VOLUME;
    if (bytes[VERSION_FIELD] >= 16 && length >= SETTINGS_FILE_SIZE &&
        hash_load(bytes + 74) == hash_update(HASH_START, bytes + 58, 16) &&
        (bytes[58] & 0xF0u) == 0 && (bytes[58] >> 2) < 3 && (bytes[59] & 0xF8u) == 0 &&
        bytes[60] <= SETTINGS_VOLUME_MAX + 1) {
        settings->status_bar_compact = (bytes[58] & 1u) != 0;
        settings->standby_enabled = (bytes[58] & 2u) != 0;
        settings->standby_power_off_option = bytes[58] >> 2;
        settings->long_press_action = bytes[59];
        settings->bluetooth_volume =
            bytes[60] == 0 ? (uint8_t)SETTINGS_DEFAULT_BLUETOOTH_VOLUME : (uint8_t)(bytes[60] - 1u);
    }
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
