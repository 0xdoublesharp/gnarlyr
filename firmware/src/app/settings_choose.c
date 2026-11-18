#include "settings_choose.h"

#include "core/i18n.h"

static int option_named(SettingsScreens *screens, const SettingChoice *choice, DebugSpan option)
{
    ChoiceConfig config = {.option_ids = choice->options, .option_count = choice->option_count};
    if (choice->options == NULL) {
        choice->extend(screens, &config);
    }
    for (int index = 0; index < config.option_count; index++) {
        const char *text =
            config.option_ids != NULL ? i18n_text(config.option_ids[index]) : config.options[index];
        if (debug_span_matches(option, text)) {
            return index;
        }
    }
    return -1;
}

static bool choose_listed(const SettingsChooseTarget *target, DebugSpan title, DebugSpan option)
{
    for (int index = 0; index < SETTINGS_CHOICE_COUNT; index++) {
        const SettingChoice *choice = &target->tables->choices[index];
        int chosen = debug_span_matches(title, i18n_text(choice->title))
                         ? option_named(target->screens, choice, option)
                         : -1;
        if (chosen >= 0) {
            choice->choose(&target->screens->context, chosen);
            return true;
        }
    }
    return false;
}

static int action_named(DebugSpan option, const StringId *actions, int count)
{
    for (int action = 0; action < count; action++) {
        if (debug_span_matches(option, i18n_text(actions[action]))) {
            return action;
        }
    }
    return -1;
}

bool settings_choose_button(const ButtonChoices *table, ButtonSettings *buttons, DebugSpan title,
                            DebugSpan option)
{
    if (debug_span_matches(title, i18n_text(table->long_title))) {
        int action = action_named(option, table->long_actions, BUTTON_LONG_ACTION_COUNT);
        if (action >= 0) {
            table->long_choose(buttons, action);
            return true;
        }
    }
    for (int button = 0; button < PLAYBACK_BUTTON_COUNT; button++) {
        if (!debug_span_matches(title, i18n_text(table->buttons[button]))) {
            continue;
        }
        int action = action_named(option, table->actions, BUTTON_ACTION_COUNT);
        if (action >= 0) {
            table->choose(&buttons->entries[button], action);
            return true;
        }
    }
    return false;
}

bool settings_choose(const SettingsChooseTarget *target, DebugSpan title, DebugSpan option)
{
    return choose_listed(target, title, option) ||
           settings_choose_button(target->tables->buttons, &target->screens->buttons, title,
                                  option);
}
