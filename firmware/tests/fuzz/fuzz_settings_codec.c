#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/hash.h"
#include "fuzz_source.h"
#include "settings/settings.h"

enum {
    VERSION_FIELD = 4,
    FLAGS_FIELD = 5,
    EXTENSION_CHECKSUM_FIELD = 54,
    SECOND_FIELD = 58,
    SECOND_CHECKSUM_FIELD = 74,
    CURRENT_VERSION = 1
};

typedef struct FuzzInput {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
} FuzzInput;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t next_byte(FuzzInput *input)
{
    if (input->size == 0) {
        return 0;
    }
    uint8_t value = input->bytes[input->offset % input->size];
    input->offset++;
    return value;
}

static void fill_primary(FuzzInput *input, Settings *settings)
{
    settings->brightness = 1u + next_byte(input) % SETTINGS_BRIGHTNESS_MAX;
    settings->screen_timeout = next_byte(input) % SCREEN_TIMEOUT_OPTION_COUNT;
    settings->gain = (GainLevel)(next_byte(input) % GAIN_LEVEL_COUNT);
    settings->output = (OutputJack)(next_byte(input) % OUTPUT_JACK_COUNT);
    settings->start_screen = (StartScreen)(next_byte(input) % START_SCREEN_COUNT);
    settings->shuffle = (ShuffleMode)(next_byte(input) % SHUFFLE_MODE_COUNT);
    settings->repeat = (RepeatMode)(next_byte(input) % REPEAT_MODE_COUNT);
    settings->volume = next_byte(input) % (SETTINGS_VOLUME_MAX + 1u);
    settings->auto_off = next_byte(input) % AUTO_OFF_OPTION_COUNT;
    settings->usb_mode = (UsbMode)(next_byte(input) % USB_MODE_COUNT);
    settings->dac_volume = next_byte(input) % (SETTINGS_VOLUME_MAX + 1u);
    settings->accent = (AccentColor)(next_byte(input) % ACCENT_COLOR_COUNT);
}

static void fill_tail(FuzzInput *input, Settings *settings)
{
    settings->appearance = (AppearanceTheme)(next_byte(input) % APPEARANCE_THEME_COUNT);
    settings->text_size = (TextSize)(next_byte(input) % TEXT_SIZE_COUNT);
    settings->skip_gesture = (SkipGesture)(next_byte(input) % SKIP_GESTURE_COUNT);
    settings->orientation = (Orientation)(next_byte(input) % ORIENTATION_COUNT);
    settings->replay_gain_mode = next_byte(input) % REPLAY_GAIN_MODE_COUNT;
    settings->prevent_clipping = (next_byte(input) & 1u) != 0;
}

static void fill_equalizer(FuzzInput *input, Settings *settings)
{
    settings->equalizer = (EqualizerPreset)(next_byte(input) % EQUALIZER_PRESET_COUNT);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        settings->equalizer_gains[band] =
            (int8_t)(next_byte(input) % (2u * SETTINGS_EQUALIZER_GAIN_MAX + 1u)) -
            SETTINGS_EQUALIZER_GAIN_MAX;
    }
}

static void fill_language(FuzzInput *input, Settings *settings)
{
    static const char CHARACTERS[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    uint8_t length = next_byte(input) % (SETTINGS_LANGUAGE_LENGTH + 1u);
    for (uint8_t index = 0; index < length; index++) {
        settings->language[index] = CHARACTERS[next_byte(input) % (sizeof CHARACTERS - 1u)];
    }
}

static Settings make_settings(FuzzInput *input)
{
    Settings settings = settings_defaults();
    fill_primary(input, &settings);
    fill_tail(input, &settings);
    fill_equalizer(input, &settings);
    fill_language(input, &settings);
    return settings;
}

static bool same_primary(const Settings *first, const Settings *second)
{
    return first->brightness == second->brightness &&
           first->screen_timeout == second->screen_timeout && first->gain == second->gain &&
           first->output == second->output && first->start_screen == second->start_screen &&
           first->shuffle == second->shuffle && first->repeat == second->repeat &&
           first->volume == second->volume && first->auto_off == second->auto_off &&
           first->usb_mode == second->usb_mode && first->dac_volume == second->dac_volume &&
           first->accent == second->accent && first->appearance == second->appearance;
}

static bool same_tail(const Settings *first, const Settings *second)
{
    if (first->equalizer != second->equalizer || first->text_size != second->text_size ||
        first->skip_gesture != second->skip_gesture || first->orientation != second->orientation ||
        first->replay_gain_mode != second->replay_gain_mode ||
        first->prevent_clipping != second->prevent_clipping ||
        memcmp(first->language, second->language, SETTINGS_LANGUAGE_CAPACITY) != 0) {
        return false;
    }
    return memcmp(first->equalizer_gains, second->equalizer_gains, sizeof first->equalizer_gains) ==
           0;
}

static bool same_settings(const Settings *first, const Settings *second)
{
    return same_primary(first, second) && same_tail(first, second);
}

static bool code_character(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-';
}

static void check_language(const Settings *settings)
{
    bool ended = false;
    for (int index = 0; index < SETTINGS_LANGUAGE_LENGTH; index++) {
        char value = settings->language[index];
        fuzz_require(value == '\0' || (!ended && code_character(value)));
        ended = ended || value == '\0';
    }
    fuzz_require(settings->language[SETTINGS_LANGUAGE_LENGTH] == '\0');
}

static void check_curve(const Settings *settings)
{
    SettingsEqualizerCurve curve = settings_equalizer_curve(settings);
    fuzz_require(curve.band_count <= SETTINGS_EQUALIZER_BANDS);
    fuzz_require(curve.pre_gain <= SETTINGS_UNITY_PRE_GAIN);
    for (uint8_t index = 0; index < curve.band_count; index++) {
        fuzz_require(index == 0 || curve.band_hz[index - 1] < curve.band_hz[index]);
        fuzz_require(curve.gain_half_db[index] >= -SETTINGS_EQUALIZER_GAIN_MAX);
        fuzz_require(curve.gain_half_db[index] <= SETTINGS_EQUALIZER_GAIN_MAX);
    }
}

static void check_gains(const Settings *settings)
{
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        fuzz_require(settings->equalizer_gains[band] >= -SETTINGS_EQUALIZER_GAIN_MAX);
        fuzz_require(settings->equalizer_gains[band] <= SETTINGS_EQUALIZER_GAIN_MAX);
    }
}

static void check_ranges(const Settings *settings)
{
    fuzz_require(settings->brightness >= SETTINGS_BRIGHTNESS_MIN &&
                 settings->brightness <= SETTINGS_BRIGHTNESS_MAX);
    fuzz_require(settings->screen_timeout < SCREEN_TIMEOUT_OPTION_COUNT);
    fuzz_require(settings->gain < GAIN_LEVEL_COUNT && settings->output < OUTPUT_JACK_COUNT);
    fuzz_require(settings->start_screen < START_SCREEN_COUNT);
    fuzz_require(settings->shuffle < SHUFFLE_MODE_COUNT && settings->repeat < REPEAT_MODE_COUNT);
    fuzz_require(settings->volume <= SETTINGS_VOLUME_MAX &&
                 settings->auto_off < AUTO_OFF_OPTION_COUNT);
    fuzz_require(settings->usb_mode < USB_MODE_COUNT &&
                 settings->dac_volume <= SETTINGS_VOLUME_MAX);
    fuzz_require(settings->accent < ACCENT_COLOR_COUNT &&
                 settings->appearance < APPEARANCE_THEME_COUNT);
    fuzz_require(settings->equalizer < EQUALIZER_PRESET_COUNT &&
                 settings->text_size < TEXT_SIZE_COUNT);
    fuzz_require(settings->skip_gesture < SKIP_GESTURE_COUNT &&
                 settings->orientation < ORIENTATION_COUNT);
    fuzz_require(settings->replay_gain_mode < REPLAY_GAIN_MODE_COUNT);
    check_language(settings);
    check_gains(settings);
    check_curve(settings);
}

static bool decode_settings(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    memset(settings, 0xA5, sizeof *settings);
    uint8_t before[sizeof *settings];
    memcpy(before, settings, sizeof before);
    bool decoded = settings_decode(bytes, length, settings);
    if (!decoded) {
        fuzz_require(memcmp(settings, before, sizeof before) == 0);
    } else {
        check_ranges(settings);
    }
    return decoded;
}

static void canonical_round_trip(const Settings *settings)
{
    uint8_t first[SETTINGS_FILE_SIZE];
    uint8_t second[SETTINGS_FILE_SIZE];
    Settings decoded;
    settings_encode(settings, first);
    fuzz_require(decode_settings(first, sizeof first, &decoded));
    fuzz_require(same_settings(settings, &decoded));
    settings_encode(&decoded, second);
    fuzz_require(memcmp(first, second, sizeof first) == 0);
}

static void mutate_record(uint8_t *bytes, int first, int field, int hash_start, FuzzInput *input)
{
    int selected = first + next_byte(input) % (uint8_t)(field - first);
    bytes[selected] ^= (uint8_t)(1u << (next_byte(input) & 7u));
    hash_store(bytes + field,
               hash_update(HASH_START, bytes + hash_start, (size_t)(field - hash_start)));
}

static void check_mutated_settings(const uint8_t *bytes, int first, int field, int hash_start,
                                   FuzzInput *input)
{
    uint8_t changed[SETTINGS_FILE_SIZE];
    Settings decoded;
    memcpy(changed, bytes, sizeof changed);
    mutate_record(changed, first, field, hash_start, input);
    if (decode_settings(changed, sizeof changed, &decoded)) {
        canonical_round_trip(&decoded);
    }
}

static void check_current(FuzzInput *input)
{
    Settings original = make_settings(input);
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings decoded;
    settings_encode(&original, bytes);
    fuzz_require(decode_settings(bytes, sizeof bytes, &decoded));
    fuzz_require(same_settings(&original, &decoded));
    canonical_round_trip(&decoded);
    check_mutated_settings(bytes, FLAGS_FIELD, EXTENSION_CHECKSUM_FIELD, 0, input);
    check_mutated_settings(bytes, SECOND_FIELD, SECOND_CHECKSUM_FIELD, SECOND_FIELD, input);
}

static void check_wrong_version(FuzzInput *input)
{
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings decoded;
    Settings original = make_settings(input);
    settings_encode(&original, bytes);
    uint8_t version = next_byte(input);
    bytes[VERSION_FIELD] = version == CURRENT_VERSION ? 0 : version;
    hash_store(bytes + EXTENSION_CHECKSUM_FIELD,
               hash_update(HASH_START, bytes, EXTENSION_CHECKSUM_FIELD));
    fuzz_require(!decode_settings(bytes, sizeof bytes, &decoded));
}

static bool same_buttons(const Settings *first, const Settings *second)
{
    return memcmp(first->button_actions, second->button_actions, sizeof first->button_actions) == 0;
}

static void check_button_ranges(const Settings *settings)
{
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        fuzz_require(settings->button_actions[index] < BUTTON_ACTION_COUNT);
    }
}

static bool decode_buttons(const uint8_t *bytes, uint32_t length, const Settings *base,
                           Settings *settings)
{
    *settings = *base;
    memset(settings->button_actions, 0xA5, sizeof settings->button_actions);
    uint8_t before[sizeof *settings];
    memcpy(before, settings, sizeof before);
    bool decoded = settings_buttons_decode(bytes, length, settings);
    if (!decoded) {
        fuzz_require(memcmp(settings, before, sizeof before) == 0);
    } else {
        fuzz_require(same_settings(settings, base));
        check_button_ranges(settings);
    }
    return decoded;
}

static void fill_buttons(FuzzInput *input, Settings *settings)
{
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        settings->button_actions[index] = next_byte(input) % BUTTON_ACTION_COUNT;
    }
}

static void check_buttons(FuzzInput *input)
{
    Settings original = make_settings(input);
    Settings decoded;
    uint8_t first[SETTINGS_BUTTONS_FILE_SIZE];
    uint8_t second[SETTINGS_BUTTONS_FILE_SIZE];
    fill_buttons(input, &original);
    settings_buttons_encode(&original, first);
    fuzz_require(decode_buttons(first, sizeof first, &original, &decoded));
    fuzz_require(same_buttons(&original, &decoded));
    settings_buttons_encode(&decoded, second);
    fuzz_require(memcmp(first, second, sizeof first) == 0);
    mutate_record(first, FLAGS_FIELD, 28, 0, input);
    decode_buttons(first, sizeof first, &original, &decoded);
}

static void check_raw(const uint8_t *data, size_t size)
{
    Settings decoded;
    Settings base = settings_defaults();
    if (decode_settings(data, (uint32_t)size, &decoded)) {
        canonical_round_trip(&decoded);
    }
    decode_buttons(data, (uint32_t)size, &base, &decoded);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_require(size <= UINT32_MAX);
    FuzzInput input = {.bytes = data, .size = size, .offset = size > 0 ? 1u : 0u};
    check_raw(data, size);
    switch (size > 0 ? data[0] & 3u : 0u) {
    case 1:
        check_current(&input);
        break;
    case 2:
        check_wrong_version(&input);
        break;
    case 3:
        check_buttons(&input);
        break;
    default:
        break;
    }
    return 0;
}
