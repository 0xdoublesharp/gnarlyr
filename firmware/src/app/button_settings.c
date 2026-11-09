#include "button_settings.h"

#include <stddef.h>

#include "ui/icons.h"

static const char *const ACTIONS[BUTTON_ACTION_COUNT] = {
    "Off",      "Play / Pause", "Up Next", "Seek", "Next track", "Previous track",
    "Volume +", "Volume -",     "Home",
};
static const char *const BUTTONS[PLAYBACK_BUTTON_COUNT] = {
    "Volume+ tap", "Volume+ hold", "Volume- tap", "Volume- hold",
    "Play tap",    "Play hold",    "Menu tap",    "Menu hold",
};
static const uint8_t KEY_ICONS[PLAYBACK_BUTTON_COUNT / 2] = {ICON_KEY_PLUS, ICON_KEY_MINUS,
                                                             ICON_KEY_PLAY, ICON_KEY_MENU};

static int current(const void *context)
{
    const ButtonEntry *entry = context;
    return entry->owner->config.settings->button_actions[entry->button];
}

static const char *value(const void *context)
{
    int action = current(context);
    return action >= 0 && action < BUTTON_ACTION_COUNT ? ACTIONS[action] : ACTIONS[0];
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
    ChoiceConfig choice = {.title = BUTTONS[entry->button],
                           .options = ACTIONS,
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
        buttons->items[index] = (MenuItem){.label = BUTTONS[index],
                                           .select = open,
                                           .context = &buttons->entries[index],
                                           .value = value,
                                           .opens = true,
                                           .icon = KEY_ICONS[index / 2]};
    }
    buttons->items[PLAYBACK_BUTTON_COUNT] = (MenuItem){
        .label = "Reset buttons", .select = reset, .context = buttons, .icon = ICON_RESCAN};
    MenuConfig menu = {.title = "Controls",
                       .items = buttons->items,
                       .item_count = PLAYBACK_BUTTON_COUNT + 1,
                       .theme = config.theme};
    return menu_screen_init(&buttons->menu, menu);
}
