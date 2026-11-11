#include "playing_options.h"

#include "core/i18n.h"
#include "hal/clock.h"

static int current_speed(const void *context)
{
    const PlayingOptions *options = context;
    return options->config.store->settings.playback_speed;
}

static const char *speed_value(const void *context)
{
    const ChoiceScreen *choice = context;
    return i18n_text(SETTINGS_SPEED_LABELS[choice->config.current(choice->config.context)]);
}

static void choose_speed(void *context, int index)
{
    PlayingOptions *options = context;
    SettingsStore *store = options->config.store;
    store->settings.playback_speed = (uint8_t)index;
    player_set_speed(options->config.player, settings_speed_percent(index));
    settings_store_capture(store, player_status(options->config.player));
    if (!settings_store_save(store)) {
        toast_show(options->config.toast, i18n_text(STR_SETTINGS_NOT_SAVED),
                   hal_clock_milliseconds());
    }
}

static void build_speed(PlayingOptions *options)
{
    ChoiceConfig speed = {.title = i18n_text(STR_PLAYBACK_SPEED),
                          .option_ids = SETTINGS_SPEED_LABELS,
                          .option_count = SETTINGS_SPEED_COUNT,
                          .current = current_speed,
                          .choose = choose_speed,
                          .context = options,
                          .theme = options->config.theme};
    choice_screen_init(&options->speed, speed);
    options->items[PLAYING_OPTIONS_SPEED_ITEM] = (MenuItem){.label = i18n_text(STR_PLAYBACK_SPEED),
                                                            .select = menu_open_screen,
                                                            .context = &options->speed.screen,
                                                            .value = speed_value,
                                                            .opens = true};
}

Screen *playing_options_init(PlayingOptions *options, PlayingOptionsConfig config)
{
    options->config = config;
    build_speed(options);
    options->items[PLAYING_OPTIONS_LYRICS_ITEM] = (MenuItem){.label = i18n_text(STR_LYRICS),
                                                             .select = menu_open_screen,
                                                             .context = config.lyrics,
                                                             .opens = true};
    MenuConfig menu = {.title = i18n_text(STR_PLAYING_OPTIONS),
                       .items = options->items,
                       .item_count = PLAYING_OPTIONS_ITEM_COUNT,
                       .theme = config.theme};
    return menu_screen_init(&options->menu, menu);
}
