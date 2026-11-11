#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "settings/settings.h"

enum {
    VERSION_FIELD = 4,
    BRIGHTNESS_FIELD = 6,
    AUTO_OFF_FIELD = 14,
    USB_MODE_FIELD = 15,
    DAC_VOLUME_FIELD = 16,
    ACCENT_FIELD = 17,
    EQUALIZER_FIELD = 18,
    EQUALIZER_GAINS_FIELD = 19,
    OLD_CHECKSUM_FIELD = 20,
    TEXT_SIZE_FIELD = 24,
    SKIP_GESTURE_FIELD = 25,
    ORIENTATION_FIELD = 26,
    PREVIOUS_CHECKSUM_FIELD = 28,
    HALF_DB_GAINS_FIELD = 28,
    HALF_DB_CHECKSUM_FIELD = 38,
    HALF_DB_VERSION = 11,
    CURRENT_VERSION = 15,
    EXTENSION_VERSION = 15,
    EXTENSION_FIELD = 38,
    EXTENSION_CHECKSUM_FIELD = 54,
    LANGUAGE_VERSION = 12,
    LANGUAGE_FIELD = 19,
    ACCENT_VERSION = 5,
    EQUALIZER_VERSION = 6,
    TEXT_SIZE_VERSION = 7,
    SKIP_GESTURE_VERSION = 8,
    VOLUME_SCALE_VERSION = 9,
    ORIENTATION_VERSION = 10,
    VOLUME_FIELD = 13,
    OLD_FILE_SIZE = 24,
    PREVIOUS_FILE_SIZE = 32,
    NEVER_OPTION = 6
};

static Settings custom_settings(void)
{
    Settings settings = settings_defaults();
    settings.brightness = 3;
    settings.screen_timeout = 4;
    settings.gain = GAIN_HIGH;
    settings.output = OUTPUT_BALANCED;
    settings.start_screen = START_FAVORITES;
    settings.shuffle = SHUFFLE_ALBUMS;
    settings.repeat = REPEAT_ONE;
    settings.volume = 42;
    settings.auto_off = 3;
    settings.usb_mode = USB_MODE_DAC;
    settings.dac_volume = 37;
    settings.accent = ACCENT_TEAL;
    settings.equalizer = EQUALIZER_JAZZ;
    settings.equalizer_gains[0] = -20;
    settings.equalizer_gains[2] = 8;
    settings.equalizer_gains[4] = 20;
    settings.equalizer_gains[9] = -3;
    settings.text_size = TEXT_SIZE_LARGE;
    settings.skip_gesture = SKIP_GESTURE_PLAY_CLICKS;
    settings.orientation = ORIENTATION_PORTRAIT_FLIPPED;
    return settings;
}

static void rewrite_checksum(uint8_t *bytes)
{
    int field = bytes[VERSION_FIELD] < EQUALIZER_VERSION   ? OLD_CHECKSUM_FIELD
                : bytes[VERSION_FIELD] < HALF_DB_VERSION   ? PREVIOUS_CHECKSUM_FIELD
                : bytes[VERSION_FIELD] < EXTENSION_VERSION ? HALF_DB_CHECKSUM_FIELD
                                                           : EXTENSION_CHECKSUM_FIELD;
    uint32_t checksum = hash_update(HASH_START, bytes, (uint32_t)field);
    for (int index = 0; index < 4; index++) {
        bytes[field + index] = (uint8_t)(checksum >> (8 * index));
    }
}

static void old_layout(uint8_t *bytes, uint8_t version)
{
    bytes[VERSION_FIELD] = version;
    bytes[VERSION_FIELD + 1] = 0;
    int first_new = version < ACCENT_VERSION      ? ACCENT_FIELD
                    : version < EQUALIZER_VERSION ? EQUALIZER_FIELD
                    : version < TEXT_SIZE_VERSION ? TEXT_SIZE_FIELD
                                                  : SKIP_GESTURE_FIELD;
    for (int index = first_new; index < SETTINGS_FILE_SIZE; index++) {
        bytes[index] = 0;
    }
    static const uint8_t OLD_JAZZ_GAINS[] = {0, 10, 14, 10, 20};
    for (int band = 0; version >= EQUALIZER_VERSION && band < 5; band++) {
        bytes[EQUALIZER_GAINS_FIELD + band] = OLD_JAZZ_GAINS[band];
    }
    rewrite_checksum(bytes);
}

static void defaults_are_comfortable(void)
{
    Settings settings = settings_defaults();
    CHECK_EQUAL(7, settings.brightness);
    CHECK_EQUAL(30000, settings_screen_timeout_ms(&settings));
    CHECK_EQUAL(GAIN_LOW, settings.gain);
    CHECK_EQUAL(OUTPUT_AUTO, settings.output);
    CHECK_EQUAL(START_MAIN_MENU, settings.start_screen);
    CHECK_EQUAL(SHUFFLE_OFF, settings.shuffle);
    CHECK_EQUAL(REPEAT_OFF, settings.repeat);
    CHECK_EQUAL(13, settings.volume);
    CHECK_EQUAL(5u * 60000u, settings_auto_off_ms(&settings));
    CHECK_EQUAL(USB_MODE_STORAGE, settings.usb_mode);
    CHECK_EQUAL(35, settings.dac_volume);
    CHECK_EQUAL(ACCENT_BLUE, settings.accent);
    CHECK_EQUAL(EQUALIZER_OFF, settings.equalizer);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(0, settings.equalizer_gains[band]);
    }
    CHECK_EQUAL(TEXT_SIZE_MEDIUM, settings.text_size);
    CHECK_EQUAL(SKIP_GESTURE_HOLD, settings.skip_gesture);
    CHECK_EQUAL(ORIENTATION_LANDSCAPE, settings.orientation);
}

static void auto_off_options_are_stocks(void)
{
    CHECK_EQUAL(0, settings_auto_off_option_minutes(0));
    CHECK_EQUAL(1, settings_auto_off_option_minutes(1));
    CHECK_EQUAL(5, settings_auto_off_option_minutes(2));
    CHECK_EQUAL(10, settings_auto_off_option_minutes(AUTO_OFF_OPTION_COUNT - 1));
}

static void a_version_one_file_keeps_its_values_and_gets_the_default_auto_off(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 1;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[AUTO_OFF_FIELD] = 0;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(49, decoded.volume);
    CHECK_EQUAL(settings_defaults().auto_off, decoded.auto_off);
    CHECK_EQUAL(USB_MODE_STORAGE, decoded.usb_mode);
    CHECK_EQUAL(TEXT_SIZE_MEDIUM, decoded.text_size);
}

static void a_version_two_file_keeps_its_auto_off_and_uses_usb_storage(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 2;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[USB_MODE_FIELD] = 0;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(3, decoded.auto_off);
    CHECK_EQUAL(USB_MODE_STORAGE, decoded.usb_mode);
}

static void the_usb_mode_and_dac_volume_are_saved_at_bytes_fifteen_and_sixteen(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(CURRENT_VERSION, bytes[VERSION_FIELD]);
    CHECK_EQUAL(USB_MODE_DAC, bytes[USB_MODE_FIELD]);
    CHECK_EQUAL(37, bytes[DAC_VOLUME_FIELD]);
}

static void a_version_three_file_keeps_its_usb_mode_and_gets_the_default_dac_volume(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 3;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[DAC_VOLUME_FIELD] = 0;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(USB_MODE_DAC, decoded.usb_mode);
    CHECK_EQUAL(SETTINGS_DEFAULT_DAC_VOLUME, decoded.dac_volume);
}

static void the_accent_color_is_saved_at_byte_seventeen(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(ACCENT_TEAL, bytes[ACCENT_FIELD]);
}

static void a_version_four_file_keeps_its_dac_volume_and_gets_the_blue_accent(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 4;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[ACCENT_FIELD] = 0;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(43, decoded.dac_volume);
    CHECK_EQUAL(ACCENT_BLUE, decoded.accent);
}

static void an_unknown_accent_color_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[ACCENT_FIELD] = ACCENT_COLOR_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void a_dac_volume_above_the_top_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[DAC_VOLUME_FIELD] = SETTINGS_VOLUME_MAX + 1;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void an_unknown_usb_mode_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[USB_MODE_FIELD] = USB_MODE_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void a_newer_version_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = CURRENT_VERSION + 1;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_equalizer_is_saved_after_the_dac_volume(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(EQUALIZER_JAZZ, bytes[EQUALIZER_FIELD]);
    CHECK_EQUAL(0, bytes[HALF_DB_GAINS_FIELD]);
    CHECK_EQUAL(20, bytes[HALF_DB_GAINS_FIELD + 1]);
    CHECK_EQUAL(28, bytes[HALF_DB_GAINS_FIELD + 2]);
    CHECK_EQUAL(40, bytes[HALF_DB_GAINS_FIELD + 4]);
    CHECK_EQUAL(17, bytes[HALF_DB_GAINS_FIELD + 9]);
    CHECK_EQUAL(0, bytes[EQUALIZER_GAINS_FIELD]);
}

static void a_version_four_file_of_the_old_size_loads_with_the_equalizer_off(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, 4);
    Settings decoded;
    CHECK(settings_decode(bytes, OLD_FILE_SIZE, &decoded));
    CHECK_EQUAL(43, decoded.dac_volume);
    CHECK_EQUAL(EQUALIZER_OFF, decoded.equalizer);
    CHECK_EQUAL(0, decoded.equalizer_gains[4]);
    CHECK_EQUAL(ACCENT_BLUE, decoded.accent);
}

static void a_version_five_file_keeps_its_accent_and_gets_the_equalizer_off(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, ACCENT_VERSION);
    Settings decoded;
    CHECK(settings_decode(bytes, OLD_FILE_SIZE, &decoded));
    CHECK_EQUAL(43, decoded.dac_volume);
    CHECK_EQUAL(ACCENT_TEAL, decoded.accent);
    CHECK_EQUAL(EQUALIZER_OFF, decoded.equalizer);
    CHECK_EQUAL(0, decoded.equalizer_gains[0]);
    CHECK_EQUAL(TEXT_SIZE_MEDIUM, decoded.text_size);
}

static void the_text_size_is_saved_at_byte_twenty_four(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(TEXT_SIZE_LARGE, bytes[TEXT_SIZE_FIELD]);
    settings.text_size = TEXT_SIZE_SMALL;
    settings_encode(&settings, bytes);
    CHECK_EQUAL(TEXT_SIZE_SMALL, bytes[TEXT_SIZE_FIELD]);
}

static void a_version_six_file_keeps_its_equalizer_and_gets_medium_text(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, EQUALIZER_VERSION);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(ACCENT_TEAL, decoded.accent);
    CHECK_EQUAL(EQUALIZER_JAZZ, decoded.equalizer);
    CHECK_EQUAL(-20, decoded.equalizer_gains[0]);
    CHECK_EQUAL(20, decoded.equalizer_gains[9]);
    CHECK_EQUAL(TEXT_SIZE_MEDIUM, decoded.text_size);
}

static void a_version_ten_equalizer_is_spread_over_ten_bands_in_half_decibels(void)
{
    static const int8_t expected[SETTINGS_EQUALIZER_BANDS] = {-20, -20, -16, -4, 3,
                                                              8,   3,   4,   16, 20};
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, ORIENTATION_VERSION);
    Settings decoded;
    CHECK(settings_decode(bytes, PREVIOUS_FILE_SIZE, &decoded));
    CHECK_EQUAL(EQUALIZER_JAZZ, decoded.equalizer);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(expected[band], decoded.equalizer_gains[band]);
    }
}

static void a_flat_version_ten_equalizer_stays_flat(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, ORIENTATION_VERSION);
    for (int band = 0; band < 5; band++) {
        bytes[EQUALIZER_GAINS_FIELD + band] = 10;
    }
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, PREVIOUS_FILE_SIZE, &decoded));
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(0, decoded.equalizer_gains[band]);
    }
}

static void a_version_ten_gain_beyond_ten_decibels_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, ORIENTATION_VERSION);
    bytes[EQUALIZER_GAINS_FIELD + 3] = 21;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, PREVIOUS_FILE_SIZE, &settings));
}

static void the_new_version_needs_the_longer_file(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK(!settings_decode(bytes, PREVIOUS_FILE_SIZE, &settings));
}

static void a_version_six_file_ignores_what_lies_at_the_text_size_byte(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, EQUALIZER_VERSION);
    bytes[TEXT_SIZE_FIELD] = TEXT_SIZE_COUNT;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(TEXT_SIZE_MEDIUM, decoded.text_size);
}

static void an_unknown_text_size_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[TEXT_SIZE_FIELD] = TEXT_SIZE_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_skip_gesture_is_saved_at_byte_twenty_five(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(SKIP_GESTURE_PLAY_CLICKS, bytes[SKIP_GESTURE_FIELD]);
    settings.skip_gesture = SKIP_GESTURE_DOUBLE_VOLUME;
    settings_encode(&settings, bytes);
    CHECK_EQUAL(SKIP_GESTURE_DOUBLE_VOLUME, bytes[SKIP_GESTURE_FIELD]);
}

static void a_version_seven_file_keeps_its_text_size_and_gets_the_hold_gesture(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, TEXT_SIZE_VERSION);
    bytes[SKIP_GESTURE_FIELD] = SKIP_GESTURE_COUNT;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(TEXT_SIZE_LARGE, decoded.text_size);
    CHECK_EQUAL(SKIP_GESTURE_HOLD, decoded.skip_gesture);
}

static Settings decoded_version_eight(uint8_t volume, uint8_t dac_volume)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = SKIP_GESTURE_VERSION;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[VOLUME_FIELD] = volume;
    bytes[DAC_VOLUME_FIELD] = dac_volume;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    return decoded;
}

static void a_version_eight_volume_converts_to_the_sixty_step_scale(void)
{
    Settings decoded = decoded_version_eight(50, 30);
    CHECK_EQUAL(59, decoded.volume);
    CHECK_EQUAL(SETTINGS_DEFAULT_DAC_VOLUME, decoded.dac_volume);
    decoded = decoded_version_eight(0, 25);
    CHECK_EQUAL(0, decoded.volume);
    CHECK_EQUAL(29, decoded.dac_volume);
    decoded = decoded_version_eight(12, 1);
    CHECK_EQUAL(13, decoded.volume);
    CHECK_EQUAL(1, decoded.dac_volume);
    CHECK_EQUAL(TEXT_SIZE_LARGE, decoded.text_size);
    CHECK_EQUAL(SKIP_GESTURE_PLAY_CLICKS, decoded.skip_gesture);
}

static void a_version_eight_volume_above_fifty_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = SKIP_GESTURE_VERSION;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[VOLUME_FIELD] = 51;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void a_current_volume_up_to_sixty_is_kept(void)
{
    Settings settings = custom_settings();
    settings.volume = SETTINGS_VOLUME_MAX;
    settings.dac_volume = 1;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(SETTINGS_VOLUME_MAX, decoded.volume);
    CHECK_EQUAL(1, decoded.dac_volume);
}

static void an_unknown_skip_gesture_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[SKIP_GESTURE_FIELD] = SKIP_GESTURE_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_orientation_is_saved_at_byte_twenty_six(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(ORIENTATION_PORTRAIT_FLIPPED, bytes[ORIENTATION_FIELD]);
    settings.orientation = ORIENTATION_PORTRAIT;
    settings_encode(&settings, bytes);
    CHECK_EQUAL(ORIENTATION_PORTRAIT, bytes[ORIENTATION_FIELD]);
}

static void a_version_nine_file_keeps_its_values_and_starts_in_landscape(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = VOLUME_SCALE_VERSION;
    bytes[VERSION_FIELD + 1] = 0;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(SKIP_GESTURE_PLAY_CLICKS, decoded.skip_gesture);
    CHECK_EQUAL(42, decoded.volume);
    CHECK_EQUAL(ORIENTATION_LANDSCAPE, decoded.orientation);
}

static void an_unknown_orientation_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[ORIENTATION_FIELD] = ORIENTATION_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_language_code_is_saved_at_bytes_nineteen_to_twenty_three(void)
{
    Settings settings = custom_settings();
    strcpy(settings.language, "pt-BR");
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK(memcmp(bytes + LANGUAGE_FIELD, "pt-BR", SETTINGS_LANGUAGE_LENGTH) == 0);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK(strcmp("pt-BR", decoded.language) == 0);
}

static void a_short_language_code_is_padded_with_zeros(void)
{
    Settings settings = custom_settings();
    strcpy(settings.language, "de");
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL('e', bytes[LANGUAGE_FIELD + 1]);
    CHECK_EQUAL(0, bytes[LANGUAGE_FIELD + 2]);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK(strcmp("de", decoded.language) == 0);
}

static void the_default_language_is_the_image_default(void)
{
    CHECK_EQUAL('\0', settings_defaults().language[0]);
}

static void a_version_eleven_file_keeps_its_values_and_gets_the_default_language(void)
{
    Settings settings = custom_settings();
    strcpy(settings.language, "de");
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = HALF_DB_VERSION;
    bytes[VERSION_FIELD + 1] = 0;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL('\0', decoded.language[0]);
    CHECK_EQUAL(ORIENTATION_PORTRAIT_FLIPPED, decoded.orientation);
    CHECK_EQUAL(-3, decoded.equalizer_gains[9]);
}

static void a_version_ten_file_does_not_read_its_gains_as_a_language(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    old_layout(bytes, ORIENTATION_VERSION);
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, PREVIOUS_FILE_SIZE, &decoded));
    CHECK_EQUAL('\0', decoded.language[0]);
}

static bool decodes_with_language_bytes(const char *raw)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    memcpy(bytes + LANGUAGE_FIELD, raw, SETTINGS_LANGUAGE_LENGTH);
    rewrite_checksum(bytes);
    return settings_decode(bytes, sizeof bytes, &settings);
}

static void a_language_code_with_other_characters_is_rejected(void)
{
    CHECK(decodes_with_language_bytes("zh-TW"));
    CHECK(!decodes_with_language_bytes("e/n\0\0"));
    CHECK(!decodes_with_language_bytes("e.\0\0\0"));
    CHECK(!decodes_with_language_bytes("\xC3\xA9\0\0\0"));
}

static void a_language_code_with_text_after_its_end_is_rejected(void)
{
    CHECK(!decodes_with_language_bytes("en\0x\0"));
    CHECK(!decodes_with_language_bytes("\0\0\0\0x"));
}

static void a_short_file_of_the_new_version_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK(!settings_decode(bytes, OLD_FILE_SIZE, &settings));
}

static void an_unknown_equalizer_preset_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[EQUALIZER_FIELD] = EQUALIZER_PRESET_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void a_custom_gain_beyond_ten_decibels_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[HALF_DB_GAINS_FIELD + 3] = 41;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void equalizer_off_is_a_flat_curve(void)
{
    Settings settings = settings_defaults();
    SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
    CHECK_EQUAL(0, curve.band_count);
    CHECK_EQUAL(SETTINGS_UNITY_PRE_GAIN, curve.pre_gain);
}

static void check_band(SettingsEqualizerCurve curve, int band, int hz, int gain_half_db)
{
    CHECK_EQUAL(hz, curve.band_hz[band]);
    CHECK_EQUAL(gain_half_db, curve.gain_half_db[band]);
}

static void bass_boosts_63_and_125_hz_with_stocks_pre_attenuation(void)
{
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_BASS;
    SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
    CHECK_EQUAL(2, curve.band_count);
    check_band(curve, 0, 63, 10);
    check_band(curve, 1, 125, 6);
    CHECK_EQUAL(SETTINGS_UNITY_PRE_GAIN * 7 / 16, curve.pre_gain);
}

static void jazz_uses_all_five_stock_bands(void)
{
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_JAZZ;
    SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
    CHECK_EQUAL(5, curve.band_count);
    check_band(curve, 0, 100, 8);
    check_band(curve, 1, 315, -12);
    check_band(curve, 2, 1000, 8);
    check_band(curve, 3, 3150, 2);
    check_band(curve, 4, 10000, 12);
    CHECK_EQUAL(SETTINGS_UNITY_PRE_GAIN * 7 / 16, curve.pre_gain);
}

static void presets_keep_stocks_pre_attenuation(void)
{
    static const int thirty_seconds[EQUALIZER_CUSTOM] = {32, 21, 14, 20, 20, 14, 14};
    Settings settings = settings_defaults();
    for (int preset = EQUALIZER_OFF; preset < EQUALIZER_CUSTOM; preset++) {
        settings.equalizer = (EqualizerPreset)preset;
        CHECK_EQUAL(thirty_seconds[preset] * 1024, settings_equalizer_curve(&settings).pre_gain);
    }
}

static void custom_skips_flat_bands(void)
{
    Settings settings = custom_settings();
    settings.equalizer = EQUALIZER_CUSTOM;
    SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
    CHECK_EQUAL(4, curve.band_count);
    check_band(curve, 0, 31, -20);
    check_band(curve, 1, 125, 8);
    check_band(curve, 2, 500, 20);
    check_band(curve, 3, 16000, -3);
}

static uint16_t custom_pre_gain(const int8_t *gains)
{
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_CUSTOM;
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        settings.equalizer_gains[band] = gains[band];
    }
    return settings_equalizer_curve(&settings).pre_gain;
}

static bool within_percent(int expected, int actual, int percent)
{
    int difference = expected > actual ? expected - actual : actual - expected;
    return difference * 100 <= expected * percent;
}

static void custom_with_one_boost_attenuates_by_that_boost_and_the_margin(void)
{
    static const int8_t three[SETTINGS_EQUALIZER_BANDS] = {6};
    static const int8_t far_apart[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 0, 0, 0, 6};
    CHECK(within_percent(18427, custom_pre_gain(three), 1));
    CHECK(within_percent(18427, custom_pre_gain(far_apart), 1));
}

static void custom_attenuates_more_for_a_bigger_peak(void)
{
    static const int8_t one[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 0, 1};
    static const int8_t ten[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 0, 20};
    static const int8_t every_band[SETTINGS_EQUALIZER_BANDS] = {20, 20, 20, 20, 20,
                                                                20, 20, 20, 20, 20};
    CHECK(custom_pre_gain(one) < SETTINGS_UNITY_PRE_GAIN);
    CHECK(custom_pre_gain(ten) < custom_pre_gain(one));
    CHECK(custom_pre_gain(every_band) < custom_pre_gain(ten));
    CHECK(within_percent(1803, custom_pre_gain(every_band), 2));
}

static void custom_with_only_cuts_keeps_headroom_that_grows_with_the_cut(void)
{
    static const int8_t shallow[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 0, -2};
    static const int8_t deep[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 0, -20};
    static const int8_t adjacent[SETTINGS_EQUALIZER_BANDS] = {-20, -20, 0, 0, 0};
    CHECK(within_percent(25001, custom_pre_gain(shallow), 1));
    CHECK(custom_pre_gain(deep) < custom_pre_gain(shallow));
    CHECK(within_percent(17396, custom_pre_gain(deep), 1));
    CHECK(custom_pre_gain(adjacent) < custom_pre_gain(deep));
}

static void flat_custom_is_a_flat_curve(void)
{
    Settings settings = settings_defaults();
    settings.equalizer = EQUALIZER_CUSTOM;
    SettingsEqualizerCurve curve = settings_equalizer_curve(&settings);
    CHECK_EQUAL(0, curve.band_count);
    CHECK_EQUAL(SETTINGS_UNITY_PRE_GAIN, curve.pre_gain);
}

static void timeout_options_end_with_never(void)
{
    CHECK_EQUAL(10, settings_timeout_option_seconds(0));
    CHECK_EQUAL(300, settings_timeout_option_seconds(5));
    CHECK_EQUAL(0, settings_timeout_option_seconds(NEVER_OPTION));
    Settings settings = settings_defaults();
    settings.screen_timeout = NEVER_OPTION;
    CHECK_EQUAL(0, settings_screen_timeout_ms(&settings));
}

static void sleep_options_start_with_off(void)
{
    CHECK_EQUAL(0, settings_sleep_option_minutes(0));
    CHECK_EQUAL(15, settings_sleep_option_minutes(1));
    CHECK_EQUAL(120, settings_sleep_option_minutes(SLEEP_TIMER_OPTION_COUNT - 1));
}

static void settings_survive_a_round_trip(void)
{
    Settings original = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings decoded;
    settings_encode(&original, bytes);
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(original.brightness, decoded.brightness);
    CHECK_EQUAL(original.screen_timeout, decoded.screen_timeout);
    CHECK_EQUAL(original.gain, decoded.gain);
    CHECK_EQUAL(original.output, decoded.output);
    CHECK_EQUAL(original.start_screen, decoded.start_screen);
    CHECK_EQUAL(original.shuffle, decoded.shuffle);
    CHECK_EQUAL(original.repeat, decoded.repeat);
    CHECK_EQUAL(original.volume, decoded.volume);
    CHECK_EQUAL(original.auto_off, decoded.auto_off);
    CHECK_EQUAL(original.usb_mode, decoded.usb_mode);
    CHECK_EQUAL(original.dac_volume, decoded.dac_volume);
    CHECK_EQUAL(original.accent, decoded.accent);
    CHECK_EQUAL(original.equalizer, decoded.equalizer);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(original.equalizer_gains[band], decoded.equalizer_gains[band]);
    }
    CHECK_EQUAL(original.text_size, decoded.text_size);
    CHECK_EQUAL(original.skip_gesture, decoded.skip_gesture);
    CHECK_EQUAL(original.orientation, decoded.orientation);
}

static void a_corrupted_file_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[BRIGHTNESS_FIELD] ^= 0x02;
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    CHECK(!settings_decode(bytes, SETTINGS_FILE_SIZE - 1, &settings));
}

static void out_of_range_values_are_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[BRIGHTNESS_FIELD] = SETTINGS_BRIGHTNESS_MAX + 1;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_theme_version_keeps_the_existing_file_size(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 13;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[27] = 1;
    rewrite_checksum(bytes);
    CHECK(settings_decode(bytes, SETTINGS_V14_FILE_SIZE, &settings));
}

static void a_theme_survives_saving_without_moving_existing_settings(void)
{
    Settings settings = custom_settings();
    settings.appearance = APPEARANCE_SUNSET;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(APPEARANCE_SUNSET, bytes[27]);
    CHECK(settings_decode(bytes, sizeof bytes, &settings));
    CHECK_EQUAL(APPEARANCE_SUNSET, settings.appearance);
    CHECK_EQUAL(42, settings.volume);
    bytes[27] = APPEARANCE_THEME_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void a_previous_version_uses_the_default_theme(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 12;
    bytes[VERSION_FIELD + 1] = 0;
    bytes[27] = 255;
    rewrite_checksum(bytes);
    CHECK(settings_decode(bytes, sizeof bytes, &settings));
    CHECK_EQUAL(APPEARANCE_NEON_NOIR, settings.appearance);
}

static void button_maps_survive_a_separate_bounded_record(void)
{
    Settings settings = custom_settings();
    settings.button_actions[PLAYBACK_PLAY_HOLD] = BUTTON_ACTION_UP_NEXT;
    uint8_t bytes[SETTINGS_BUTTONS_FILE_SIZE];
    settings_buttons_encode(&settings, bytes);
    Settings loaded = settings_defaults();
    CHECK(settings_buttons_decode(bytes, sizeof bytes, &loaded));
    CHECK_EQUAL(BUTTON_ACTION_UP_NEXT, settings_button_action(&loaded, PLAYBACK_PLAY_HOLD));
    CHECK_EQUAL(BUTTON_ACTION_DEFAULT, settings_button_action(&loaded, PLAYBACK_PLAY_TAP));
    CHECK(!settings_buttons_decode(bytes, sizeof bytes - 1, &loaded));
    bytes[8] = BUTTON_ACTION_COUNT;
    hash_store(bytes + 28, hash_update(HASH_START, bytes, 28));
    CHECK(!settings_buttons_decode(bytes, sizeof bytes, &loaded));
    CHECK_EQUAL(BUTTON_ACTION_UP_NEXT, settings_button_action(&loaded, PLAYBACK_PLAY_HOLD));
}

static void normalization_defaults_off_with_peak_protection_and_migrates_version_thirteen(void)
{
    Settings defaults = settings_defaults();
    CHECK_EQUAL(REPLAY_GAIN_OFF, defaults.replay_gain_mode);
    CHECK(defaults.prevent_clipping);
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings original = custom_settings();
    settings_encode(&original, bytes);
    bytes[4] = 13;
    bytes[5] = 0;
    rewrite_checksum(bytes);
    Settings restored;
    CHECK(settings_decode(bytes, sizeof bytes, &restored));
    CHECK_EQUAL(REPLAY_GAIN_OFF, restored.replay_gain_mode);
    CHECK(restored.prevent_clipping);
    CHECK_EQUAL(original.volume, restored.volume);
    CHECK_EQUAL(original.orientation, restored.orientation);
    CHECK_EQUAL(original.equalizer, restored.equalizer);
}

static void normalization_modes_and_peak_protection_round_trip_in_the_flags_byte(void)
{
    for (uint8_t mode = REPLAY_GAIN_OFF; mode < REPLAY_GAIN_MODE_COUNT; mode++) {
        for (uint8_t clipping = 0; clipping < 2; clipping++) {
            Settings chosen = custom_settings();
            chosen.replay_gain_mode = mode;
            chosen.prevent_clipping = clipping != 0;
            uint8_t bytes[SETTINGS_FILE_SIZE];
            settings_encode(&chosen, bytes);
            CHECK_EQUAL(mode | (clipping << 2), bytes[5]);
            Settings restored;
            CHECK(settings_decode(bytes, sizeof bytes, &restored));
            CHECK_EQUAL(mode, restored.replay_gain_mode);
            CHECK_EQUAL(clipping, restored.prevent_clipping);
            CHECK_EQUAL(chosen.volume, restored.volume);
        }
    }
}

static void invalid_normalization_flags_are_rejected_without_mutating_the_settings(void)
{
    static const uint8_t FLAGS[] = {3, 7, 8, 128, 255};
    for (uint32_t at = 0; at < sizeof FLAGS / sizeof FLAGS[0]; at++) {
        Settings original = custom_settings();
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&original, bytes);
        bytes[5] = FLAGS[at];
        rewrite_checksum(bytes);
        Settings before = original;
        CHECK(!settings_decode(bytes, sizeof bytes, &original));
        CHECK(memcmp(&before, &original, sizeof original) == 0);
    }
}

static Settings extension_settings(void)
{
    Settings settings = custom_settings();
    settings.rewind_on_resume = false;
    settings.safe_start_volume = false;
    settings.safe_start_volume_level = SETTINGS_VOLUME_MAX;
    settings.clock_24h = false;
    settings.track_transition = TRACK_TRANSITION_CROSSFADE;
    settings.transition_tenths = SETTINGS_TRANSITION_TENTHS_MAX;
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        settings.home_order[index] = (uint8_t)(SETTINGS_HOME_ITEM_COUNT - 1 - index);
    }
    settings.home_hidden = 0x1F7;
    return settings;
}

static void check_extension_defaults(const Settings *settings)
{
    CHECK(settings->rewind_on_resume);
    CHECK(settings->safe_start_volume);
    CHECK_EQUAL(24, settings->safe_start_volume_level);
    CHECK(settings->clock_24h);
    CHECK_EQUAL(TRACK_TRANSITION_OFF, settings->track_transition);
    CHECK_EQUAL(15, settings->transition_tenths);
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        CHECK_EQUAL(index, settings->home_order[index]);
    }
    CHECK_EQUAL(0, settings->home_hidden);
}

static void the_extension_defaults_keep_todays_behaviour(void)
{
    Settings settings = settings_defaults();
    check_extension_defaults(&settings);
    CHECK_EQUAL(SETTINGS_VOLUME_MAX * 2 / 5, settings.safe_start_volume_level);
}

static void the_extension_block_sits_between_the_language_and_the_checksum(void)
{
    static const uint8_t EXPECTED[16] = {0x07, 24, 15, 0, 0x10, 0x32, 0x54, 0x76, 0x08};
    Settings settings = settings_defaults();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(58, sizeof bytes);
    CHECK_EQUAL(15, bytes[VERSION_FIELD]);
    CHECK(memcmp(bytes + EXTENSION_FIELD, EXPECTED, sizeof EXPECTED) == 0);
    CHECK_EQUAL(hash_update(HASH_START, bytes, EXTENSION_CHECKSUM_FIELD),
                hash_load(bytes + EXTENSION_CHECKSUM_FIELD));
}

static void every_extension_field_survives_a_round_trip(void)
{
    static const uint8_t EXPECTED[16] = {0x10, 60, 60, 0, 0x78, 0x56, 0x34, 0x12, 0x00, 0xF7, 0x01};
    Settings original = extension_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&original, bytes);
    CHECK(memcmp(bytes + EXTENSION_FIELD, EXPECTED, sizeof EXPECTED) == 0);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK(!decoded.rewind_on_resume);
    CHECK(!decoded.safe_start_volume);
    CHECK_EQUAL(SETTINGS_VOLUME_MAX, decoded.safe_start_volume_level);
    CHECK(!decoded.clock_24h);
    CHECK_EQUAL(TRACK_TRANSITION_CROSSFADE, decoded.track_transition);
    CHECK_EQUAL(SETTINGS_TRANSITION_TENTHS_MAX, decoded.transition_tenths);
    CHECK(memcmp(original.home_order, decoded.home_order, sizeof decoded.home_order) == 0);
    CHECK_EQUAL(0x1F7, decoded.home_hidden);
    CHECK_EQUAL(original.volume, decoded.volume);
}

static void each_comfort_flag_and_the_fade_round_trip_alone(void)
{
    for (int flag = 0; flag < 4; flag++) {
        Settings original = extension_settings();
        original.rewind_on_resume = flag == 0;
        original.safe_start_volume = flag == 1;
        original.clock_24h = flag == 2;
        original.track_transition = flag == 3 ? TRACK_TRANSITION_FADE : TRACK_TRANSITION_OFF;
        original.transition_tenths = SETTINGS_TRANSITION_TENTHS_MIN;
        original.safe_start_volume_level = 0;
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&original, bytes);
        CHECK_EQUAL(1 << flag, bytes[EXTENSION_FIELD]);
        Settings decoded;
        CHECK(settings_decode(bytes, sizeof bytes, &decoded));
        CHECK_EQUAL(original.rewind_on_resume, decoded.rewind_on_resume);
        CHECK_EQUAL(original.safe_start_volume, decoded.safe_start_volume);
        CHECK_EQUAL(original.clock_24h, decoded.clock_24h);
        CHECK_EQUAL(original.track_transition, decoded.track_transition);
        CHECK_EQUAL(SETTINGS_TRANSITION_TENTHS_MIN, decoded.transition_tenths);
        CHECK_EQUAL(0, decoded.safe_start_volume_level);
    }
}

static void a_version_fourteen_file_loads_with_the_extension_defaults_and_saves_as_fifteen(void)
{
    Settings original = extension_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&original, bytes);
    bytes[VERSION_FIELD] = 14;
    bytes[VERSION_FIELD + 1] = REPLAY_GAIN_ALBUM;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(settings_decode(bytes, SETTINGS_V14_FILE_SIZE, &decoded));
    check_extension_defaults(&decoded);
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, decoded.replay_gain_mode);
    CHECK_EQUAL(original.volume, decoded.volume);
    CHECK_EQUAL(original.orientation, decoded.orientation);
    settings_encode(&decoded, bytes);
    CHECK_EQUAL(15, bytes[VERSION_FIELD]);
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    check_extension_defaults(&decoded);
}

static void every_older_version_loads_with_the_extension_defaults(void)
{
    for (uint8_t version = 1; version < EXTENSION_VERSION; version++) {
        Settings original = extension_settings();
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&original, bytes);
        old_layout(bytes, version);
        if (version >= LANGUAGE_VERSION) {
            memset(bytes + LANGUAGE_FIELD, 0, SETTINGS_LANGUAGE_LENGTH);
            rewrite_checksum(bytes);
        }
        Settings decoded;
        CHECK(settings_decode(bytes, sizeof bytes, &decoded));
        check_extension_defaults(&decoded);
        CHECK_EQUAL(original.brightness, decoded.brightness);
    }
}

static void a_version_fifteen_file_needs_all_fifty_eight_bytes(void)
{
    Settings settings = settings_defaults();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK(!settings_decode(bytes, SETTINGS_V14_FILE_SIZE, &settings));
    CHECK(!settings_decode(bytes, SETTINGS_FILE_SIZE - 1, &settings));
}

static void the_checksum_covers_the_extension_block(void)
{
    for (int offset = EXTENSION_FIELD; offset < EXTENSION_CHECKSUM_FIELD; offset++) {
        Settings settings = settings_defaults();
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&settings, bytes);
        bytes[offset] ^= 0x01;
        CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    }
}

static void each_invalid_extension_value_is_rejected_without_mutating_the_settings(void)
{
    static const uint8_t CASES[][2] = {
        {0, 0x20},  {0, 0x40},  {0, 0x80}, {0, 0x18}, {1, 61},   {2, 4},    {2, 61},   {2, 0},
        {3, 1},     {4, 0x00},  {4, 0x11}, {6, 0x55}, {8, 0x18}, {8, 0x09}, {9, 0x08}, {9, 0xFF},
        {10, 0x02}, {10, 0x80}, {11, 1},   {12, 1},   {13, 1},   {14, 1},   {15, 0x80}};
    for (uint32_t at = 0; at < sizeof CASES / sizeof CASES[0]; at++) {
        Settings original = settings_defaults();
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&original, bytes);
        bytes[EXTENSION_FIELD + CASES[at][0]] = CASES[at][1];
        rewrite_checksum(bytes);
        Settings before = original;
        CHECK(!settings_decode(bytes, sizeof bytes, &original));
        CHECK(memcmp(&before, &original, sizeof original) == 0);
    }
}

static void every_item_but_settings_may_be_hidden(void)
{
    for (int item = 0; item < SETTINGS_HOME_ITEM_COUNT; item++) {
        Settings original = settings_defaults();
        original.home_hidden = (uint16_t)(1u << item);
        uint8_t bytes[SETTINGS_FILE_SIZE];
        settings_encode(&original, bytes);
        Settings decoded;
        CHECK_EQUAL(item != SETTINGS_HOME_SETTINGS_ITEM,
                    settings_decode(bytes, sizeof bytes, &decoded));
    }
}

static void pack_home(uint8_t *packed, const uint8_t *order, int count, uint16_t hidden)
{
    memset(packed, 0, SETTINGS_HOME_PACKED_SIZE);
    for (int index = 0; index < count; index++) {
        packed[index / 2] |= (uint8_t)(order[index] << (4 * (index & 1)));
    }
    packed[5] = (uint8_t)hidden;
    packed[6] = (uint8_t)(hidden >> 8);
}

static void a_saved_home_order_and_hidden_mask_are_applied_on_load(void)
{
    static const uint8_t ORDER[] = {4, 3, 0, 8, 7, 6, 5, 2, 1};
    Settings original = settings_defaults();
    memcpy(original.home_order, ORDER, sizeof ORDER);
    original.home_hidden = 1u << 2 | 1u << 8;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&original, bytes);
    Settings decoded = settings_defaults();
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK(memcmp(ORDER, decoded.home_order, sizeof ORDER) == 0);
    CHECK_EQUAL(1u << 2 | 1u << 8, decoded.home_hidden);
}

static void an_order_from_a_build_with_more_items_drops_the_unknown_ones(void)
{
    static const uint8_t ORDER[] = {9, 4, 0, 1, 2, 3, 5, 6, 7, 8};
    static const uint8_t KNOWN[] = {4, 0, 1, 2, 3, 5, 6, 7, 8};
    uint8_t packed[SETTINGS_HOME_PACKED_SIZE];
    pack_home(packed, ORDER, 10, 1u << 9 | 1u << 2);
    uint8_t order[SETTINGS_HOME_ORDER_SLOTS];
    uint16_t hidden = 0;
    CHECK(settings_home_unpack(packed, SETTINGS_HOME_ITEM_COUNT, order, &hidden));
    CHECK(memcmp(KNOWN, order, sizeof KNOWN) == 0);
    CHECK_EQUAL(1u << 2, hidden);
    CHECK(settings_home_unpack(packed, 10, order, &hidden));
    CHECK(memcmp(ORDER, order, sizeof ORDER) == 0);
    CHECK_EQUAL(1u << 9 | 1u << 2, hidden);
}

static void a_settings_file_from_a_build_with_more_home_items_keeps_every_other_setting(void)
{
    static const uint8_t ORDER[] = {6, 9, 0, 1, 2, 3, 4, 5, 7, 8};
    static const uint8_t KNOWN[] = {6, 0, 1, 2, 3, 4, 5, 7, 8};
    Settings original = extension_settings();
    original.home_hidden = 0;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&original, bytes);
    pack_home(bytes + EXTENSION_FIELD + 4, ORDER, 10, 1u << 9 | 1u << 7);
    rewrite_checksum(bytes);
    Settings decoded = settings_defaults();
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK(memcmp(KNOWN, decoded.home_order, sizeof KNOWN) == 0);
    CHECK_EQUAL(1u << 7, decoded.home_hidden);
    CHECK_EQUAL(original.volume, decoded.volume);
    CHECK_EQUAL(original.track_transition, decoded.track_transition);
    CHECK_EQUAL(original.transition_tenths, decoded.transition_tenths);
}

static void an_order_saved_with_fewer_items_gains_the_new_ones_at_the_end_visible(void)
{
    static const uint8_t ORDER[] = {8, 7, 6, 5, 4, 3, 2, 1, 0};
    static const uint8_t MIGRATED[] = {8, 7, 6, 5, 4, 3, 2, 1, 0, 9};
    uint8_t packed[SETTINGS_HOME_PACKED_SIZE];
    pack_home(packed, ORDER, 9, 1u << 2);
    uint8_t order[SETTINGS_HOME_ORDER_SLOTS];
    uint16_t hidden = 0;
    CHECK(settings_home_unpack(packed, 10, order, &hidden));
    CHECK(memcmp(MIGRATED, order, sizeof MIGRATED) == 0);
    CHECK_EQUAL(1u << 2, hidden);
}

static void a_ten_item_build_may_hide_its_tenth_item_but_never_settings(void)
{
    static const uint8_t ORDER[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t packed[SETTINGS_HOME_PACKED_SIZE];
    uint8_t order[SETTINGS_HOME_ORDER_SLOTS];
    uint16_t hidden = 0;
    pack_home(packed, ORDER, 10, 1u << 9);
    CHECK(settings_home_unpack(packed, 10, order, &hidden));
    CHECK_EQUAL(1u << 9, hidden);
    pack_home(packed, ORDER, 10, 1u << SETTINGS_HOME_SETTINGS_ITEM);
    CHECK(!settings_home_unpack(packed, 10, order, &hidden));
}

static void a_hidden_item_the_saving_build_did_not_have_is_rejected(void)
{
    static const uint8_t ORDER[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t packed[SETTINGS_HOME_PACKED_SIZE];
    pack_home(packed, ORDER, 9, 1u << 9);
    uint8_t order[SETTINGS_HOME_ORDER_SLOTS];
    uint16_t hidden = 0;
    CHECK(!settings_home_unpack(packed, 10, order, &hidden));
}

static void orders_that_are_not_a_permutation_of_the_first_items_are_rejected(void)
{
    static const uint8_t SHORT[] = {0, 1, 2, 3, 4, 5, 6, 7};
    static const uint8_t REPEATED[] = {0, 1, 2, 3, 4, 5, 6, 7, 7};
    static const uint8_t GAP[] = {0, 1, 2, 3, 4, 5, 6, 7, 9};
    static const uint8_t TRAILING[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 1};
    const uint8_t *cases[] = {SHORT, REPEATED, GAP, TRAILING};
    const int counts[] = {8, 9, 9, 10};
    for (int at = 0; at < 4; at++) {
        uint8_t packed[SETTINGS_HOME_PACKED_SIZE];
        pack_home(packed, cases[at], counts[at], 0);
        uint8_t order[SETTINGS_HOME_ORDER_SLOTS];
        uint16_t hidden = 0;
        CHECK(!settings_home_unpack(packed, 10, order, &hidden));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(defaults_are_comfortable),
        TEST_CASE(the_extension_defaults_keep_todays_behaviour),
        TEST_CASE(the_extension_block_sits_between_the_language_and_the_checksum),
        TEST_CASE(every_extension_field_survives_a_round_trip),
        TEST_CASE(each_comfort_flag_and_the_fade_round_trip_alone),
        TEST_CASE(a_version_fourteen_file_loads_with_the_extension_defaults_and_saves_as_fifteen),
        TEST_CASE(every_older_version_loads_with_the_extension_defaults),
        TEST_CASE(a_version_fifteen_file_needs_all_fifty_eight_bytes),
        TEST_CASE(the_checksum_covers_the_extension_block),
        TEST_CASE(each_invalid_extension_value_is_rejected_without_mutating_the_settings),
        TEST_CASE(every_item_but_settings_may_be_hidden),
        TEST_CASE(a_saved_home_order_and_hidden_mask_are_applied_on_load),
        TEST_CASE(an_order_from_a_build_with_more_items_drops_the_unknown_ones),
        TEST_CASE(a_settings_file_from_a_build_with_more_home_items_keeps_every_other_setting),
        TEST_CASE(an_order_saved_with_fewer_items_gains_the_new_ones_at_the_end_visible),
        TEST_CASE(a_ten_item_build_may_hide_its_tenth_item_but_never_settings),
        TEST_CASE(a_hidden_item_the_saving_build_did_not_have_is_rejected),
        TEST_CASE(orders_that_are_not_a_permutation_of_the_first_items_are_rejected),
        TEST_CASE(normalization_defaults_off_with_peak_protection_and_migrates_version_thirteen),
        TEST_CASE(normalization_modes_and_peak_protection_round_trip_in_the_flags_byte),
        TEST_CASE(invalid_normalization_flags_are_rejected_without_mutating_the_settings),
        TEST_CASE(the_theme_version_keeps_the_existing_file_size),
        TEST_CASE(a_theme_survives_saving_without_moving_existing_settings),
        TEST_CASE(a_previous_version_uses_the_default_theme),
        TEST_CASE(button_maps_survive_a_separate_bounded_record),
        TEST_CASE(timeout_options_end_with_never),
        TEST_CASE(sleep_options_start_with_off),
        TEST_CASE(settings_survive_a_round_trip),
        TEST_CASE(a_corrupted_file_is_rejected),
        TEST_CASE(out_of_range_values_are_rejected),
        TEST_CASE(auto_off_options_are_stocks),
        TEST_CASE(a_version_one_file_keeps_its_values_and_gets_the_default_auto_off),
        TEST_CASE(a_version_two_file_keeps_its_auto_off_and_uses_usb_storage),
        TEST_CASE(the_usb_mode_and_dac_volume_are_saved_at_bytes_fifteen_and_sixteen),
        TEST_CASE(a_version_three_file_keeps_its_usb_mode_and_gets_the_default_dac_volume),
        TEST_CASE(a_dac_volume_above_the_top_is_rejected),
        TEST_CASE(an_unknown_usb_mode_is_rejected),
        TEST_CASE(a_newer_version_is_rejected),
        TEST_CASE(a_version_eight_volume_converts_to_the_sixty_step_scale),
        TEST_CASE(a_version_eight_volume_above_fifty_is_rejected),
        TEST_CASE(a_current_volume_up_to_sixty_is_kept),
        TEST_CASE(the_accent_color_is_saved_at_byte_seventeen),
        TEST_CASE(a_version_four_file_keeps_its_dac_volume_and_gets_the_blue_accent),
        TEST_CASE(an_unknown_accent_color_is_rejected),
        TEST_CASE(a_version_five_file_keeps_its_accent_and_gets_the_equalizer_off),
        TEST_CASE(the_equalizer_is_saved_after_the_dac_volume),
        TEST_CASE(a_version_four_file_of_the_old_size_loads_with_the_equalizer_off),
        TEST_CASE(a_short_file_of_the_new_version_is_rejected),
        TEST_CASE(the_language_code_is_saved_at_bytes_nineteen_to_twenty_three),
        TEST_CASE(a_short_language_code_is_padded_with_zeros),
        TEST_CASE(the_default_language_is_the_image_default),
        TEST_CASE(a_version_eleven_file_keeps_its_values_and_gets_the_default_language),
        TEST_CASE(a_version_ten_file_does_not_read_its_gains_as_a_language),
        TEST_CASE(a_language_code_with_other_characters_is_rejected),
        TEST_CASE(a_language_code_with_text_after_its_end_is_rejected),
        TEST_CASE(the_text_size_is_saved_at_byte_twenty_four),
        TEST_CASE(a_version_six_file_keeps_its_equalizer_and_gets_medium_text),
        TEST_CASE(a_version_six_file_ignores_what_lies_at_the_text_size_byte),
        TEST_CASE(an_unknown_text_size_is_rejected),
        TEST_CASE(the_skip_gesture_is_saved_at_byte_twenty_five),
        TEST_CASE(a_version_seven_file_keeps_its_text_size_and_gets_the_hold_gesture),
        TEST_CASE(an_unknown_skip_gesture_is_rejected),
        TEST_CASE(the_orientation_is_saved_at_byte_twenty_six),
        TEST_CASE(a_version_nine_file_keeps_its_values_and_starts_in_landscape),
        TEST_CASE(an_unknown_orientation_is_rejected),
        TEST_CASE(a_version_ten_equalizer_is_spread_over_ten_bands_in_half_decibels),
        TEST_CASE(a_flat_version_ten_equalizer_stays_flat),
        TEST_CASE(a_version_ten_gain_beyond_ten_decibels_is_rejected),
        TEST_CASE(the_new_version_needs_the_longer_file),
        TEST_CASE(an_unknown_equalizer_preset_is_rejected),
        TEST_CASE(a_custom_gain_beyond_ten_decibels_is_rejected),
        TEST_CASE(equalizer_off_is_a_flat_curve),
        TEST_CASE(bass_boosts_63_and_125_hz_with_stocks_pre_attenuation),
        TEST_CASE(jazz_uses_all_five_stock_bands),
        TEST_CASE(presets_keep_stocks_pre_attenuation),
        TEST_CASE(custom_skips_flat_bands),
        TEST_CASE(custom_with_one_boost_attenuates_by_that_boost_and_the_margin),
        TEST_CASE(custom_attenuates_more_for_a_bigger_peak),
        TEST_CASE(custom_with_only_cuts_keeps_headroom_that_grows_with_the_cut),
        TEST_CASE(flat_custom_is_a_flat_curve),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
