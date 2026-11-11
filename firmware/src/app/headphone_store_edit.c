#include "headphone_store.h"

#include "hal/audio.h"
#include "hal/system.h"

static bool lower_volumes(HeadphoneStore *store, HeadphoneAudio next, Player *player,
                          SettingsStore *settings)
{
    uint8_t music = headphone_audio_switch_volume(store->active, next, player->volume);
    uint8_t usb = headphone_audio_switch_volume(store->active, next, settings->settings.dac_volume);
    bool changed = player->volume != music;
    player_change_volume(player, (int)music - player->volume);
    settings->settings.dac_volume = usb;
    settings_store_capture(settings, player_status(player));
    hal_system_set_dac_volume(usb);
    bool saved = settings_store_save(settings);
    return (!changed || !player->has_track || player_save(player)) && saved;
}

bool headphone_store_commit(HeadphoneStore *store, const HeadphoneProfiles *profiles,
                            Player *player, SettingsStore *settings)
{
    if (!headphone_profiles_valid(profiles) || store->file.needs_reload) {
        return false;
    }
    HeadphoneAudio next = headphone_store_parameters(profiles);
    if (!lower_volumes(store, next, player, settings) ||
        headphone_file_save(&store->file, profiles) != STORE_DONE) {
        return false;
    }
    store->profiles = *profiles;
    store->active = next;
    hal_audio_set_headphones(next);
    return true;
}
