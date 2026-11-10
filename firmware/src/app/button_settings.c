#include "button_settings.h"

#include <stddef.h>

#include "core/i18n.h"

static const StringId ACTIONS[BUTTON_ACTION_COUNT] = {
    STR_BUTTON_DEFAULT,     STR_OFF,         STR_BUTTON_PLAY_PAUSE, STR_UP_NEXT,
    STR_BUTTON_SEEK,        STR_BUTTON_NEXT, STR_BUTTON_PREVIOUS,   STR_BUTTON_VOLUME_UP,
    STR_BUTTON_VOLUME_DOWN, STR_BUTTON_HOME,
};
static const StringId BUTTONS[PLAYBACK_BUTTON_COUNT] = {
    STR_BUTTON_VOLUME_UP_TAP,    STR_BUTTON_VOLUME_UP_HOLD, STR_BUTTON_VOLUME_DOWN_TAP,
    STR_BUTTON_VOLUME_DOWN_HOLD, STR_BUTTON_PLAY_TAP,       STR_BUTTON_PLAY_HOLD,
    STR_BUTTON_MENU_TAP,         STR_BUTTON_MENU_HOLD,
};

static int current(const void *context)
{
    const ButtonEntry *entry = context;
    return entry->owner->config.settings->button_actions[entry->button];
}

static const char *value(const void *context)
{
    int action = current(context);
    return i18n_text(ACTIONS[action >= 0 && action < BUTTON_ACTION_COUNT ? action : 0]);
}

static void choose(void *context, int action)
{
    ButtonEntry *entry = context;
    ButtonSettingsConfig *config = &entry->owner->config;
    if (action < 0 || action >= BUTTON_ACTION_COUNT) {
        return;
    }
    config->settings->button_actions[entry->button] = (uint8_t)action;
    if (config->changed != NULL) {
        config->changed(config->context);
    }
}

static ScreenAction open(void *context)
{
    ButtonEntry *entry = context;
    ButtonSettings *buttons = entry->owner;
    ChoiceConfig choice = {.title = i18n_text(BUTTONS[entry->button]),
                           .option_ids = ACTIONS,
                           .option_count = BUTTON_ACTION_COUNT,
                           .current = current,
                           .choose = choose,
                           .context = entry,
                           .theme = buttons->config.theme};
    return screen_action_push(choice_screen_init(&buttons->choice, choice));
}

static ScreenAction reset(void *context)
{
    ButtonSettings *buttons = context;
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        buttons->config.settings->button_actions[index] =
            (uint8_t)settings_default_button_action((PlaybackButton)index);
    }
    if (buttons->config.changed != NULL) {
        buttons->config.changed(buttons->config.context);
    }
    return screen_action_none();
}

Screen *button_settings_init(ButtonSettings *buttons, ButtonSettingsConfig config)
{
    buttons->config = config;
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        buttons->entries[index] = (ButtonEntry){.owner = buttons, .button = (PlaybackButton)index};
        buttons->items[index] = (MenuItem){
            .label = i18n_text(BUTTONS[index]),
            .select = open,
            .context = &buttons->entries[index],
            .value = value,
            .opens = true,
        };
    }
    buttons->items[PLAYBACK_BUTTON_COUNT] =
        (MenuItem){.label = i18n_text(STR_RESET_BUTTONS), .select = reset, .context = buttons};
    MenuConfig menu = {.title = i18n_text(STR_CONTROLS),
                       .items = buttons->items,
                       .item_count = PLAYBACK_BUTTON_COUNT + 1,
                       .theme = config.theme,
                       .detail_below = true};
    return menu_screen_init(&buttons->menu, menu);
}

bool button_settings_choose(ButtonSettings *buttons, DebugSpan title, DebugSpan option)
{
    for (int button = 0; button < PLAYBACK_BUTTON_COUNT; button++) {
        if (!debug_span_matches(title, i18n_text(BUTTONS[button]))) {
            continue;
        }
        for (int action = 0; action < BUTTON_ACTION_COUNT; action++) {
            if (debug_span_matches(option, i18n_text(ACTIONS[action]))) {
                choose(&buttons->entries[button], action);
                return true;
            }
        }
    }
    return false;
}
