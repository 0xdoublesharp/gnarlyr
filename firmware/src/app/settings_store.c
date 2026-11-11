#include "settings_store.h"

#include "equalizer_presets.h"
#include "hal/audio.h"
#include "hal/display.h"
#include "hal/storage.h"
#include "hal/system.h"

const StringId EQUALIZER_PRESET_NAMES[EQUALIZER_PRESET_COUNT] = {
    STR_OFF,     STR_EQ_RETRO,  STR_EQ_BASS,   STR_EQ_HEAVY,  STR_EQ_POP,
    STR_EQ_JAZZ, STR_EQ_UNIQUE, STR_EQ_CUSTOM, STR_PEQ_CUSTOM};

const char SETTINGS_STORE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/settings.bin";
const char SETTINGS_BUTTONS_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/buttons.bin";

static const HalAudioGain HAL_GAINS[GAIN_LEVEL_COUNT] = {HAL_AUDIO_GAIN_LOW, HAL_AUDIO_GAIN_HIGH};
static const HalAudioOutput HAL_OUTPUTS[OUTPUT_JACK_COUNT] = {
    HAL_AUDIO_OUTPUT_AUTO, HAL_AUDIO_OUTPUT_SINGLE_ENDED, HAL_AUDIO_OUTPUT_BALANCED};
static const HalUsbMode HAL_USB_MODES[USB_MODE_COUNT] = {HAL_USB_MODE_STORAGE, HAL_USB_MODE_DAC};
static const HalUsbAnswer HAL_USB_ANSWERS[USB_CONNECT_COUNT] = {
    HAL_USB_ANSWER_ASK, HAL_USB_ANSWER_STORAGE, HAL_USB_ANSWER_CHARGE};

static const uint32_t FILE_SIZES[] = {SETTINGS_FILE_SIZE, SETTINGS_V14_FILE_SIZE,
                                      SETTINGS_PREVIOUS_FILE_SIZE};

static bool read_music_volume(uint8_t *bytes, uint32_t length)
{
    HalFile file;
    if (!hal_file_open(&file, SETTINGS_STORE_PATH)) {
        return false;
    }
    bool read = file.size >= length && hal_file_read(file, 0, bytes, length);
    hal_file_close(&file);
    return read;
}

static bool read_valid(uint8_t *bytes, Settings *settings, bool internal)
{
    for (size_t index = 0; index < sizeof FILE_SIZES / sizeof FILE_SIZES[0]; index++) {
        uint32_t length = FILE_SIZES[index];
        bool read = internal ? hal_internal_file_read(SETTINGS_STORE_PATH, bytes, length)
                             : read_music_volume(bytes, length);
        if (read && settings_decode(bytes, length, settings)) {
            return true;
        }
    }
    return false;
}

static bool same_bytes(const uint8_t *first, const uint8_t *second, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

static void remember(SettingsStore *store, const uint8_t *bytes)
{
    for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
        store->saved[index] = bytes[index];
    }
    store->has_saved = true;
}

void settings_store_load(SettingsStore *store)
{
    uint8_t bytes[SETTINGS_FILE_SIZE] = {0};
    store->has_saved = false;
    store->has_saved_buttons = false;
    if (read_valid(bytes, &store->settings, true)) {
        remember(store, bytes);
    } else if (!read_valid(bytes, &store->settings, false)) {
        store->settings = settings_defaults();
    }
    parametric_store_load(&store->parametric, &store->settings.parametric);
    store->equalizer_slot = (int8_t)parametric_store_matching(&store->settings);
    uint8_t buttons[SETTINGS_BUTTONS_FILE_SIZE];
    if (hal_internal_file_read(SETTINGS_BUTTONS_PATH, buttons, sizeof buttons) &&
        settings_buttons_decode(buttons, sizeof buttons, &store->settings)) {
        for (int index = 0; index < SETTINGS_BUTTONS_FILE_SIZE; index++) {
            store->saved_buttons[index] = buttons[index];
        }
        store->has_saved_buttons = true;
    }
}

static bool save_buttons(SettingsStore *store)
{
    uint8_t bytes[SETTINGS_BUTTONS_FILE_SIZE];
    settings_buttons_encode(&store->settings, bytes);
    if (store->has_saved_buttons && same_bytes(bytes, store->saved_buttons, sizeof bytes)) {
        return true;
    }
    if (!hal_internal_file_write(SETTINGS_BUTTONS_PATH, bytes, sizeof bytes)) {
        return false;
    }
    for (int index = 0; index < SETTINGS_BUTTONS_FILE_SIZE; index++) {
        store->saved_buttons[index] = bytes[index];
    }
    store->has_saved_buttons = true;
    return true;
}

bool settings_store_save(SettingsStore *store)
{
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&store->settings, bytes);
    if (!store->has_saved || !same_bytes(bytes, store->saved, sizeof bytes)) {
        if (!hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes)) {
            return false;
        }
        remember(store, bytes);
    }
    return save_buttons(store);
}

void settings_store_capture(SettingsStore *store, PlayerStatus playback)
{
    store->settings.volume = playback.volume;
    store->settings.shuffle = playback.shuffle;
    store->settings.repeat = playback.repeat;
}

void settings_store_restore_playback(const SettingsStore *store, Player *player, uint32_t seed)
{
    const Settings *settings = &store->settings;
    player_change_volume(player, (int)settings->volume - (int)player_status(player).volume);
    player_set_shuffle(player, settings->shuffle, seed);
    player_set_repeat(player, settings->repeat);
    player_set_replay_gain(player, (ReplayGainMode)settings->replay_gain_mode,
                           settings->prevent_clipping);
    player_set_rewind(player, settings->rewind_on_resume);
    player_set_transitions(player, settings->track_transition != TRACK_TRANSITION_OFF,
                           settings->transition_tenths);
}

void settings_store_apply_equalizer(const Settings *settings)
{
    SettingsEqualizerCurve curve = settings_equalizer_curve(settings);
    HalAudioEqualizer equalizer = {.band_count = curve.band_count,
                                   .pre_gain = curve.pre_gain,
                                   .is_parametric = curve.is_parametric,
                                   .parametric = curve.parametric};
    for (int band = 0; band < curve.band_count; band++) {
        equalizer.band_hz[band] = curve.band_hz[band];
        equalizer.gain_half_db[band] = curve.gain_half_db[band];
    }
    hal_audio_set_equalizer(&equalizer);
}

void settings_store_apply_hardware(const Settings *settings)
{
    hal_display_set_brightness(settings->brightness);
    hal_audio_set_gain(HAL_GAINS[settings->gain]);
    hal_audio_set_output(HAL_OUTPUTS[settings->output]);
    hal_system_set_usb_mode(HAL_USB_MODES[settings->usb_mode]);
    hal_system_set_usb_answer(HAL_USB_ANSWERS[settings->usb_connect]);
    hal_system_set_dac_volume(settings->dac_volume);
    settings_store_apply_equalizer(settings);
}
