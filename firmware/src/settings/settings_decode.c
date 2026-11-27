#include "settings/settings.h"

#include "core/format_chain.h"
#include "core/record.h"
#include "settings_format.h"

static const FormatChain CHAIN = {.current = VERSION, .steps = NULL};

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

static void settings_read_extension(const uint8_t *extension, Settings *settings)
{
    settings->status_items = extension[STATUS_ITEMS_BYTE] & STATUS_ITEMS_BITS;
    settings->battery_care =
        (uint8_t)((extension[BATTERY_CARE_BYTE] & BATTERY_CARE_MASK) >> BATTERY_CARE_SHIFT);
    uint8_t comfort = extension[COMFORT_BYTE];
    settings->rewind_on_resume = (comfort & REWIND_BIT) != 0;
    settings->safe_start_volume = (comfort & SAFE_START_BIT) != 0;
    settings->clock_24h = (comfort & CLOCK_24H_BIT) != 0;
    settings->clock_offset_minutes = settings_clock_offset(extension);
    settings->power_on_flags = extension[POWER_ON_BYTE] & POWER_ON_BITS;
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

static bool magic_valid(const uint8_t *bytes)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != SETTINGS_MAGIC[index]) {
            return false;
        }
    }
    return true;
}

static bool header_valid(const uint8_t *bytes)
{
    if (!magic_valid(bytes)) {
        return false;
    }
    uint8_t flags = bytes[VERSION_FIELD + 1];
    return bytes[VERSION_FIELD] == VERSION && (flags & 0xC0u) == 0 &&
           (flags & 3u) < REPLAY_GAIN_MODE_COUNT && (flags >> 4u & 3u) < USB_CONNECT_COUNT &&
           record_sealed(bytes, EXTENSION_CHECKSUM_FIELD) &&
           record_sealed(bytes + SECOND_FIELD, EXTENSION_SIZE);
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
    int16_t offset = settings_clock_offset(extension);
    uint8_t speed = (uint8_t)(extension[SPEED_BYTE] + SETTINGS_SPEED_NORMAL);
    return comfort >> TRANSITION_SHIFT < TRACK_TRANSITION_COUNT && speed < SETTINGS_SPEED_COUNT &&
           extension[SAFE_LEVEL_BYTE] <= SETTINGS_VOLUME_MAX &&
           tenths >= SETTINGS_TRANSITION_TENTHS_MIN && tenths <= SETTINGS_TRANSITION_TENTHS_MAX &&
           offset >= -720 && offset <= 840 && home_valid(extension) && reserved_clear(extension);
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
    settings_read_extension(bytes + EXTENSION_FIELD, settings);
    read_second(bytes + SECOND_FIELD, settings);
    return true;
}

bool settings_buttons_decode(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (length != SETTINGS_BUTTONS_FILE_SIZE || bytes[0] != 'D' || bytes[1] != 'S' ||
        bytes[2] != 'B' || bytes[3] != 'T' || bytes[4] != 1 || bytes[5] != PLAYBACK_BUTTON_COUNT ||
        !record_sealed(bytes, 28)) {
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
    settings->headset_hold_volume = (bytes[BUTTONS_FLAGS_FIELD] & HEADSET_HOLD_VOLUME_BIT) != 0;
    return true;
}
