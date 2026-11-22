#include "button_settings.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"

static const StringId ACTIONS[BUTTON_ACTION_COUNT] = {
    STR_BUTTON_DEFAULT,
    STR_OFF,
    STR_BUTTON_PLAY_PAUSE,
    STR_UP_NEXT,
    STR_AB_LOOP,
    STR_BUTTON_NEXT,
    STR_BUTTON_PREVIOUS,
    STR_BUTTON_VOLUME_UP,
    STR_BUTTON_VOLUME_DOWN,
    STR_BUTTON_HOME,
};
static const StringId BUTTONS[PLAYBACK_BUTTON_COUNT] = {
    STR_BUTTON_VOLUME_UP_TAP,    STR_BUTTON_VOLUME_UP_HOLD, STR_BUTTON_VOLUME_DOWN_TAP,
    STR_BUTTON_VOLUME_DOWN_HOLD, STR_BUTTON_PLAY_TAP,       STR_BUTTON_PLAY_HOLD,
    STR_BUTTON_MENU_TAP,         STR_BUTTON_MENU_HOLD,
};

static const StringId DEFAULT_ACTIONS[PLAYBACK_BUTTON_COUNT] = {
    STR_BUTTON_VOLUME_UP,   STR_BUTTON_KEEP_RAISING,
    STR_BUTTON_VOLUME_DOWN, STR_BUTTON_KEEP_LOWERING,
    STR_BUTTON_PLAY_PAUSE,  STR_BUTTON_NO_ACTION,
    STR_QUICK_SETTINGS,     STR_LYRICS,
};
static const IconId ICONS[PLAYBACK_BUTTON_COUNT] = {
    ICON_SPEAKER, ICON_SPEAKER, ICON_SPEAKER,  ICON_SPEAKER,
    ICON_PLAY,    ICON_PLAY,    ICON_KEY_MENU, ICON_KEY_MENU,
};

static const StringId LONG_ACTIONS[] = {STR_LONG_CURRENT, STR_QUICK_SETTINGS, STR_UP_NEXT,
                                        STR_ADD_TO_TAPE,  STR_SHUFFLE,        STR_SLEEP_TIMER};

static int long_current(const void *context)
{
    const ButtonSettings *buttons = context;
    return buttons->config.settings->long_press_action;
}

static void long_choose(void *context, int action)
{
    ButtonSettings *buttons = context;
    if (action < 0 || action >= 6) {
        return;
    }
    buttons->config.settings->long_press_action = (uint8_t)action;
    if (buttons->config.changed != NULL) {
        buttons->config.changed(buttons->config.context);
    }
}

static const char *long_value(const void *context)
{
    int action = long_current(context);
    return i18n_text(LONG_ACTIONS[action < 6 ? action : 0]);
}

static ScreenAction long_open(void *context)
{
    ButtonSettings *buttons = context;
    ChoiceConfig choice = {.title = i18n_text(STR_MENU_LONG_ACTION),
                           .option_ids = LONG_ACTIONS,
                           .option_count = 6,
                           .current = long_current,
                           .choose = long_choose,
                           .context = buttons,
                           .theme = buttons->config.theme};
    return screen_action_push(choice_screen_init(&buttons->choice, choice));
}

static int current(const void *context)
{
    const ButtonEntry *entry = context;
    return entry->owner->config.settings->button_actions[entry->button];
}

static bool customised(int action)
{
    return action > BUTTON_ACTION_DEFAULT && action < BUTTON_ACTION_COUNT;
}

static const char *value(const void *context)
{
    const ButtonEntry *entry = context;
    int action = current(context);
    if (!customised(action)) {
        return i18n_text(DEFAULT_ACTIONS[entry->button]);
    }
    StringBuilder text = string_builder_make(entry->owner->value, BUTTON_VALUE_BYTES);
    i18n_format_text(&text, STR_BUTTON_CUSTOM_VALUE, i18n_text(ACTIONS[action]));
    return entry->owner->value;
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
    buttons->config.settings->long_press_action = 0;
    if (buttons->config.changed != NULL) {
        buttons->config.changed(buttons->config.context);
    }
    return screen_action_none();
}

static const char *skip_value(const void *context)
{
    (void)context;
    return i18n_text(STR_BUTTON_SKIP_TRACK);
}

static const char *seek_value(const void *context)
{
    (void)context;
    return i18n_text(STR_BUTTON_SEEK);
}

static MenuItem section(StringId label)
{
    return (MenuItem){.label = i18n_text(label), .role = LIST_ROW_SECTION};
}

static void bind_rows(ButtonSettings *buttons)
{
    MenuItem *items = buttons->items;
    items[0] = section(STR_NOW_PLAYING);
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        buttons->entries[index] = (ButtonEntry){.owner = buttons, .button = (PlaybackButton)index};
        items[BUTTON_ROW_FIRST + index] = (MenuItem){.label = i18n_text(BUTTONS[index]),
                                                     .select = open,
                                                     .context = &buttons->entries[index],
                                                     .value = value,
                                                     .opens = true,
                                                     .icon = ICONS[index]};
    }
    items[BUTTON_ROW_SKIP] = (MenuItem){
        .label = i18n_text(STR_BUTTON_HOLD_PLAY_VOL), .value = skip_value, .icon = ICON_PLAY};
    items[BUTTON_ROW_SEEK] = (MenuItem){
        .label = i18n_text(STR_BUTTON_HOLD_MENU_VOL), .value = seek_value, .icon = ICON_KEY_MENU};
    items[BUTTON_ROW_EVERY_SCREEN] = section(STR_BUTTONS_EVERY_SCREEN);
    items[BUTTON_ROW_LONG] = (MenuItem){.label = i18n_text(STR_MENU_LONG_ACTION),
                                        .select = long_open,
                                        .context = buttons,
                                        .value = long_value,
                                        .opens = true,
                                        .icon = ICON_KEY_MENU};
    items[BUTTON_ROW_RESET] =
        (MenuItem){.label = i18n_text(STR_RESET_BUTTONS), .select = reset, .context = buttons};
}

Screen *button_settings_init(ButtonSettings *buttons, ButtonSettingsConfig config)
{
    buttons->config = config;
    bind_rows(buttons);
    MenuConfig menu = {.title = i18n_text(STR_SETTINGS_BUTTONS),
                       .items = buttons->items,
                       .item_count = BUTTON_ROW_COUNT,
                       .theme = config.theme,
                       .detail_below = true};
    Screen *screen = menu_screen_init(&buttons->menu, menu);
    list_view_select(&buttons->menu.list, BUTTON_ROW_FIRST, 0);
    return screen;
}

#ifdef ECHO_DEBUG_LOOP
const ButtonChoices BUTTON_CHOICES = {.buttons = BUTTONS,
                                      .actions = ACTIONS,
                                      .long_actions = LONG_ACTIONS,
                                      .long_title = STR_MENU_LONG_ACTION,
                                      .choose = choose,
                                      .long_choose = long_choose};
#endif
