#pragma once

#include "settings/settings.h"

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

static const uint8_t SETTINGS_MAGIC[MAGIC_SIZE] = {'D', 'S', 'S', 'T'};

static inline int16_t settings_clock_offset(const uint8_t *extension)
{
    uint16_t offset =
        extension[CLOCK_OFFSET_BYTE] | (uint16_t)((uint16_t)extension[CLOCK_OFFSET_BYTE + 1] << 8);
    return (int16_t)(offset <= INT16_MAX ? offset : (int32_t)offset - 65536);
}

static inline void settings_read_extension(const uint8_t *extension, Settings *settings)
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
