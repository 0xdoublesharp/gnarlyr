#include "settings_preview.h"

#include "hal/audio.h"
#include "hal/display.h"
#include "settings_screens.h"

void settings_preview_apply(Settings *settings, SettingsPreview preview)
{
    switch (preview.choice) {
    case STR_THEME:
        settings->appearance = (AppearanceTheme)preview.index;
        break;
    case STR_COLOR:
        settings->accent = (AccentColor)preview.index;
        break;
    case STR_BRIGHTNESS:
        settings->brightness = (uint8_t)(preview.index + SETTINGS_BRIGHTNESS_MIN);
        break;
    case STR_TEXT_SIZE:
        settings->text_size = (TextSize)preview.index;
        break;
    case STR_ORIENTATION:
        settings->orientation = (Orientation)preview.index;
        break;
    case STR_GAIN:
        settings->gain = (GainLevel)preview.index;
        break;
    case STR_OUTPUT:
        settings->output = (OutputJack)preview.index;
        break;
    case STR_REPLAY_GAIN:
        settings->replay_gain_mode = (uint8_t)preview.index;
        break;
    case STR_PREVENT_CLIPPING:
        settings->prevent_clipping = preview.index != 0;
        break;
    default:
        break;
    }
}

static void apply_changes(const Settings *before, const Settings *after, Player *player)
{
    if (before->brightness != after->brightness) {
        hal_display_set_brightness(after->brightness);
    }
    if (before->gain != after->gain) {
        hal_audio_set_gain(after->gain == GAIN_HIGH ? HAL_AUDIO_GAIN_HIGH : HAL_AUDIO_GAIN_LOW);
    }
    if (before->output != after->output) {
        static const HalAudioOutput OUTPUTS[OUTPUT_JACK_COUNT] = {
            HAL_AUDIO_OUTPUT_AUTO, HAL_AUDIO_OUTPUT_SINGLE_ENDED, HAL_AUDIO_OUTPUT_BALANCED};
        hal_audio_set_output(OUTPUTS[after->output]);
    }
    if (before->replay_gain_mode != after->replay_gain_mode ||
        before->prevent_clipping != after->prevent_clipping) {
        player_set_replay_gain(player, (ReplayGainMode)after->replay_gain_mode,
                               after->prevent_clipping);
    }
}

void settings_preview_follow(SettingsPreview *applied, SettingsPreview next, const Settings *saved,
                             Player *player)
{
    if (applied->choice == next.choice && applied->index == next.index) {
        return;
    }
    Settings before = *saved;
    Settings after = *saved;
    settings_preview_apply(&before, *applied);
    settings_preview_apply(&after, next);
    apply_changes(&before, &after, player);
    *applied = next;
}

const Settings *settings_preview_shown(SettingsScreens *screens, bool active)
{
    static Settings shown;
    SettingsPreview next = {.choice = STRING_COUNT};
    if (active && screens->choice_preview != STRING_COUNT && screens->choice.list.selected >= 0 &&
        screens->choice.list.selected < screens->choice.config.option_count) {
        next = (SettingsPreview){screens->choice_preview, screens->choice.list.selected};
    }
    settings_preview_follow(&screens->preview, next, &screens->context.store->settings,
                            screens->context.player);
    shown = screens->context.store->settings;
    if (active) {
        settings_preview_apply(&shown, next);
    } else {
        screens->choice_preview = STRING_COUNT;
    }
    return &shown;
}
