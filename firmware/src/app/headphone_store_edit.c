#include "headphone_store.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"
#include "hal/system.h"

static bool lower_volumes(HeadphoneStore *store, HeadphoneAudio next, Player *player,
                          SettingsStore *settings)
{
    uint8_t music = headphone_audio_switch_volume(store->active, next, player->volume);
    uint8_t usb = headphone_audio_switch_volume(store->active, next, settings->settings.dac_volume);
    bool changed = player->volume != music;
    player_set_volume(player, PLAYER_OUTPUT_WIRED, music);
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

void headphone_store_choose_for_dac(HeadphoneStore *store, uint8_t chosen, Player *player,
                                    SettingsStore *settings)
{
    HeadphoneProfiles profiles = store->profiles;
    profiles.current = chosen;
    uint8_t usb = settings->settings.dac_volume;
    if (!headphone_store_commit(store, &profiles, player, settings)) {
        hal_audio_set_headphones(store->active);
        return;
    }
    settings->settings.dac_volume = usb;
    hal_system_set_dac_volume(usb);
    settings_store_save(settings);
}

void headphone_store_fill_slots(HeadphoneProfiles *profiles, uint8_t crossfeed)
{
    for (int slot = profiles->count; slot < HEADPHONE_PROFILE_LIMIT; slot++) {
        HeadphoneProfile *profile = &profiles->slots[slot];
        *profile = headphone_profile_defaults();
        profile->crossfeed = crossfeed;
        StringBuilder name = string_builder_make(profile->name, sizeof profile->name);
        i18n_format_counts(&name, STR_HEADPHONE_SLOT, (uint32_t)slot + 1, 0);
    }
    profiles->count = HEADPHONE_PROFILE_LIMIT;
}
