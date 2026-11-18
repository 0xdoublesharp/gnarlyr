#include "audio_settings.h"

#include "hal/audio.h"
#include "hal/system.h"

static void set_volumes(SettingsStore *store, Player *player, uint8_t music, uint8_t usb)
{
    player_set_volume(player, PLAYER_OUTPUT_WIRED, music);
    store->settings.dac_volume = usb;
    hal_system_set_dac_volume(usb);
}

static AudioSettingsResult save(SettingsStore *store, Player *player, bool kept)
{
    settings_store_capture(store, player_status(player));
    return settings_store_save(store) && kept ? AUDIO_SETTINGS_SAVED : AUDIO_SETTINGS_NOT_SAVED;
}

AudioSettingsResult audio_settings_set_gain(SettingsStore *store, Player *player, GainLevel gain)
{
    uint8_t music = player->volume;
    uint8_t usb = store->settings.dac_volume;
    bool rising = gain == GAIN_HIGH && store->settings.gain != GAIN_HIGH;
    if (rising) {
        HeadphoneAudio active = hal_audio_headphones();
        set_volumes(store, player,
                    headphone_audio_rise_volume(active, music, HEADPHONE_AUDIO_HIGH_GAIN_RISE_DB),
                    headphone_audio_rise_volume(active, usb, HEADPHONE_AUDIO_HIGH_GAIN_RISE_DB));
    }
    if (!hal_audio_set_gain(gain == GAIN_HIGH ? HAL_AUDIO_GAIN_HIGH : HAL_AUDIO_GAIN_LOW)) {
        if (rising) {
            set_volumes(store, player, music, usb);
        }
        return AUDIO_SETTINGS_NOT_APPLIED;
    }
    store->settings.gain = gain;
    return save(store, player,
                player->volume == music || !player->has_track || player_save(player));
}

AudioSettingsResult audio_settings_set_filter(SettingsStore *store, Player *player,
                                              DacFilter filter)
{
    if (!hal_audio_set_filter((HalAudioFilter)filter)) {
        return AUDIO_SETTINGS_NOT_APPLIED;
    }
    store->settings.dac_filter = (uint8_t)filter;
    return save(store, player, true);
}
