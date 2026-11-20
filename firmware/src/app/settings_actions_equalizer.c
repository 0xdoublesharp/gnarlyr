#include "settings_actions.h"
#include "settings_screens.h"

#include "core/i18n.h"
#include "hal/clock.h"

static const Settings *settings_of(const void *context)
{
    const SettingsContext *settings = context;
    return &settings->store->settings;
}

int settings_action_active_slot(const void *context)
{
    const SettingsContext *settings = context;
    const Settings *chosen = settings_of(context);
    return chosen->equalizer == EQUALIZER_CUSTOM
               ? equalizer_presets_matching_slot(settings->presets, chosen->equalizer_gains)
               : -1;
}

int settings_action_current_equalizer(const void *context)
{
    const SettingsContext *settings = context;
    int slot = settings_action_active_slot(context);
    return slot < 0 ? (int)settings_of(context)->equalizer
                    : equalizer_presets_slot_option(settings->presets, slot);
}

void settings_action_choose_equalizer(void *context, int index)
{
    SettingsContext *settings = context;
    Settings next;
    settings_action_candidate(settings, &next);
    int8_t gains[SETTINGS_EQUALIZER_BANDS];
    int8_t slot = -1;
    if (index >= EQUALIZER_PRESET_COUNT) {
        slot = (int8_t)equalizer_presets_option_slot(settings->presets, index);
        if (!equalizer_presets_load(settings->presets, index, gains)) {
            toast_show(settings->toast, i18n_text(STR_EQ_FILE_UNREADABLE),
                       hal_clock_milliseconds());
            return;
        }
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            next.equalizer_gains[band] = gains[band];
        }
        index = EQUALIZER_CUSTOM;
    }
    next.equalizer = (EqualizerPreset)index;
    if (settings_action_publish(settings, &next)) {
        settings->store->equalizer_slot = slot;
        settings_store_apply_hardware(&settings->store->settings);
    }
}
