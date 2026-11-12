#include "audio_settings.h"

#include "hal/audio.h"
#include "hal/system.h"

static bool lower_for_high_gain(SettingsStore *store, Player *player)
{
    HeadphoneAudio active = hal_audio_headphones();
    uint8_t music =
        headphone_audio_rise_volume(active, player->volume, AUDIO_SETTINGS_HIGH_GAIN_RISE_DB);
    uint8_t usb = headphone_audio_rise_volume(active, store->settings.dac_volume,
                                              AUDIO_SETTINGS_HIGH_GAIN_RISE_DB);
    bool changed = player->volume != music;
    player_change_volume(player, (int)music - player->volume);
    store->settings.dac_volume = usb;
    hal_system_set_dac_volume(usb);
    return !changed || !player->has_track || player_save(player);
}

static bool save(SettingsStore *store, Player *player)
{
    settings_store_capture(store, player_status(player));
    return settings_store_save(store);
}

bool audio_settings_set_gain(SettingsStore *store, Player *player, GainLevel gain)
{
    bool kept = true;
    if (gain == GAIN_HIGH && store->settings.gain != GAIN_HIGH) {
        kept = lower_for_high_gain(store, player);
    }
    store->settings.gain = gain;
    hal_audio_set_gain(gain == GAIN_HIGH ? HAL_AUDIO_GAIN_HIGH : HAL_AUDIO_GAIN_LOW);
    return save(store, player) && kept;
}

bool audio_settings_set_filter(SettingsStore *store, Player *player, DacFilter filter)
{
    store->settings.dac_filter = (uint8_t)filter;
    hal_audio_set_filter((HalAudioFilter)filter);
    return save(store, player);
}
