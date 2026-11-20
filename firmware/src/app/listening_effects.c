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
