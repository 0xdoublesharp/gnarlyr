#include "listening_effects.h"

#include "core/seam.h"

CORE_SEAM HalAudioEffects listening_effects_choose(const Settings *settings,
                                                   const HeadphoneProfiles *profiles, bool spoken)
{
    bool profiled = profiles->current < profiles->count;
    return (HalAudioEffects){.crossfeed = profiled ? profiles->slots[profiles->current].crossfeed
                                                   : settings->crossfeed,
                             .skip_silence = spoken && settings->skip_silence,
                             .speech_boost = spoken && settings->speech_boost};
}

bool listening_effects_toggle_skip_silence(SettingsStore *store)
{
    store->settings.skip_silence = !store->settings.skip_silence;
    return settings_store_save(store);
}

bool listening_effects_toggle_speech_boost(SettingsStore *store)
{
    store->settings.speech_boost = !store->settings.speech_boost;
    return settings_store_save(store);
}

static uint8_t next_crossfeed(uint8_t crossfeed)
{
    return (uint8_t)((crossfeed + 1) % HAL_AUDIO_CROSSFEED_LEVELS);
}

bool listening_effects_cycle_crossfeed(ListeningEffectsContext context)
{
    HeadphoneProfiles profiles = context.headphones->profiles;
    if (profiles.current >= profiles.count) {
        context.settings->settings.crossfeed = next_crossfeed(context.settings->settings.crossfeed);
        return settings_store_save(context.settings);
    }
    HeadphoneProfile *active = &profiles.slots[profiles.current];
    active->crossfeed = next_crossfeed(active->crossfeed);
    return headphone_store_commit(context.headphones, &profiles, context.player, context.settings);
}
