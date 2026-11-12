#include "settings_actions.h"
#include "settings_screens.h"

#include "core/i18n.h"
#include "hal/clock.h"

static const Settings *settings_of(const void *context)
{
    const SettingsContext *settings = context;
    return &settings->store->settings;
}

static Settings *editable_settings(void *context)
{
    SettingsContext *settings = context;
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

const char *settings_action_describe_equalizer(const void *context, int current)
{
    int slot = settings_action_active_slot(context);
    return slot < 0 ? i18n_text(EQUALIZER_PRESET_NAMES[current])
                    : equalizer_presets_slot_label(slot);
}

void settings_action_choose_equalizer(void *context, int index)
{
    SettingsContext *settings = context;
    Settings *chosen = editable_settings(context);
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
            chosen->equalizer_gains[band] = gains[band];
        }
        index = EQUALIZER_CUSTOM;
    }
    settings->store->equalizer_slot = slot;
    chosen->equalizer = (EqualizerPreset)index;
    settings_action_apply_and_save(context);
}
