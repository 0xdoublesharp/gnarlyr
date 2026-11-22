#include <string.h>

#include "button_settings.h"
#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "settings_choose.h"
#include "settings_screens.h"
#include "ui/label.h"

enum {
    ROW_ICON_BOX = 20,
    ROW_ICON_GAP = 4
};

static Color pixels[HAL_DISPLAY_SHORT_SIDE * HAL_DISPLAY_LONG_SIDE];
static Color expected[HAL_DISPLAY_SHORT_SIDE * 80];
static Theme theme;
static SettingsStore store;
static ButtonSettings buttons;
static int changed_count;

static bool choose_setting(SettingsScreens *screens, DebugSpan title, DebugSpan option)
{
    SettingsChooseTarget target = {screens, &SETTINGS_CHOOSE_TABLES};
    return settings_choose(&target, title, option);
}

static bool choose_control(DebugSpan title, DebugSpan option)
{
    return settings_choose_button(&BUTTON_CHOICES, &buttons, title, option);
}

static void settings_changed(void *context)
{
    CHECK(context == &changed_count);
    changed_count++;
}

static DebugSpan span_of(const char *text)
{
    return (DebugSpan){.text = text, .length = strlen(text)};
}

static int expected_row_height(void)
{
    return theme.metrics.row_height + font_line_height(theme.fonts.small) + 2;
}

static void open_controls(TextSize size, Orientation orientation)
{
    fake_hal_reset();
    i18n_detach();
    theme = BOARD_THEME;
    default_theme_set_text_size(&theme, size);
    default_theme_set_orientation(&theme, orientation);
    theme.colors.selection_shine = 0x001F;
    theme.colors.selection_top = 0x001F;
    theme.colors.selection_background = 0x001F;
    store.settings = settings_defaults();
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        store.settings.button_actions[index] = BUTTON_ACTION_PREVIOUS;
    }
    button_settings_init(&buttons,
                         (ButtonSettingsConfig){.settings = &store.settings, .theme = &theme});
    buttons.menu.screen.enter(&buttons.menu, 0);
}

static void value_rows_get_a_separate_line_in_large_portrait(void)
{
    open_controls(TEXT_SIZE_LARGE, ORIENTATION_PORTRAIT);
    int rows = theme_content_area(&theme).height / expected_row_height();
    CHECK_EQUAL(rows, buttons.menu.list.visible_rows);
    CHECK(buttons.menu.list.visible_rows < theme_visible_rows(&theme));
}

static void compare_text(const Canvas *actual, Rect area, TextStyle style, const char *text)
{
    const Font *font = style.font;
    int width = text_width(font, text);
    CHECK(width <= area.width);
    Canvas reference =
        canvas_make(expected, (Rect){0, area.y, HAL_DISPLAY_SHORT_SIDE, area.height});
    canvas_clear(&reference, theme.colors.selection_background);
    label_draw(&reference, style, (Label){area, text});
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + width; x++) {
            CHECK_EQUAL(canvas_pixel_at(&reference, x, y), canvas_pixel_at(actual, x, y));
        }
    }
}

static void every_control_label_and_value_is_shown_whole_in_portrait(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        open_controls((TextSize)size, ORIENTATION_PORTRAIT);
        MenuScreen *menu = &buttons.menu;
        int row_height = expected_row_height();
        for (int index = BUTTON_ROW_FIRST; index < BUTTON_ROW_SKIP; index++) {
            list_view_select(&menu->list, index, 0);
            menu->screen.update(menu, 0);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            menu->screen.draw(menu, &canvas);
            int y = theme.metrics.header_height + (index - menu->list.top) * row_height;
            int icon = ROW_ICON_BOX + (theme.metrics.padding - ROW_ICON_GAP > ROW_ICON_GAP
                                           ? theme.metrics.padding - ROW_ICON_GAP
                                           : ROW_ICON_GAP);
            int inset = theme.metrics.padding + icon;
            int width = theme.metrics.screen_width - 2 * theme.metrics.padding - icon;
            if (menu->model.count(menu) > menu->list.visible_rows) {
                width -= theme.metrics.scrollbar_width + theme.metrics.scrollbar_inset;
            }
            const char *label = menu->model.label(menu, index);
            const char *detail = menu->model.detail(menu, index);
            compare_text(
                &canvas, (Rect){inset, y, width, theme.metrics.row_height},
                (TextStyle){.font = theme.fonts.title, .color = theme.colors.selection_text},
                label);
            compare_text(
                &canvas,
                (Rect){inset, y + theme.metrics.row_height + 2, width - 24,
                       font_line_height(theme.fonts.small)},
                (TextStyle){.font = theme.fonts.small, .color = theme.colors.text_secondary},
                detail);
            CHECK(index >= menu->list.top);
            CHECK(index < menu->list.top + menu->list.visible_rows);
        }
    }
}

static void settings_values_use_the_same_whole_label_layout_in_large_portrait(void)
{
    open_controls(TEXT_SIZE_LARGE, ORIENTATION_PORTRAIT);
    static SettingsScreens screens;
    static Player player;
    static Library library;
    static PowerPolicy power;
    static Toast toast;
    static BluetoothLink link;
    static BluetoothScreen bluetooth;
    player_init(&player, &library);
    bluetooth_screen_init(&bluetooth, &link, &theme);
    SettingsContext context = {.store = &store,
                               .player = &player,
                               .power = &power,
                               .toast = &toast,
                               .bluetooth = &bluetooth,
                               .theme = &theme};
    settings_screens_init(&screens, context, &theme);
    const MenuItem *display = &screens.groups[SETTINGS_GROUP_DISPLAY];
    Screen *screen = display->select(display->context).target;
    CHECK(screen == &screens.group_menu.screen);
    screen->enter(screen->context, 0);
    MenuScreen *menu = &screens.group_menu;
    bool has_status_bar = false;
    for (int row = 0; row < menu->model.count(menu); row++) {
        const char *label = menu->model.label(menu, row);
        has_status_bar |= label != NULL && strcmp(label, "Status bar") == 0;
    }
    CHECK(has_status_bar);
    int index = 0;
    while (index < menu->model.count(menu) &&
           strcmp(menu->model.label(menu, index), i18n_text(STR_SCREEN_TIMEOUT)) != 0) {
        index++;
    }
    CHECK(index < menu->model.count(menu));
    list_view_select(&menu->list, index, 0);
    screen->update(screen->context, 0);
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    screen->draw(screen->context, &canvas);
    int height = expected_row_height();
    int y = theme.metrics.header_height + (index - menu->list.top) * height;
    int inset = theme.metrics.padding;
    int width = theme.metrics.screen_width - 2 * inset - theme.metrics.scrollbar_width -
                theme.metrics.scrollbar_inset;
    CHECK_EQUAL(theme_content_area(&theme).height / height, menu->list.visible_rows);
    compare_text(&canvas, (Rect){inset, y, width, theme.metrics.row_height},
                 (TextStyle){.font = theme.fonts.title, .color = theme.colors.selection_text},
                 i18n_text(STR_SCREEN_TIMEOUT));
    compare_text(&canvas,
                 (Rect){inset, y + theme.metrics.row_height + 2, width - 24,
                        font_line_height(theme.fonts.small)},
                 (TextStyle){.font = theme.fonts.small, .color = theme.colors.text_secondary},
                 menu->model.detail(menu, index));
}

static void status_bar_choices_fit_every_size_and_orientation_and_save_independently(void)
{
    static SettingsScreens screens;
    static Player player;
    static Library library;
    static PowerPolicy power;
    static Toast toast;
    static BluetoothLink link;
    static BluetoothScreen bluetooth;
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            open_controls((TextSize)size, (Orientation)orientation);
            player_init(&player, &library);
            bluetooth_screen_init(&bluetooth, &link, &theme);
            SettingsContext context = {.store = &store,
                                       .player = &player,
                                       .power = &power,
                                       .toast = &toast,
                                       .bluetooth = &bluetooth,
                                       .theme = &theme};
            settings_screens_init(&screens, context, &theme);
            CHECK(strcmp(screens.groups[SETTINGS_GROUP_BUTTONS].label,
                         i18n_text(STR_SETTINGS_BUTTONS)) == 0);
            for (int row = 0; row < SETTINGS_STATUS_COUNT; row++) {
                const MenuItem *item = &screens.status_items[row];
                CHECK(text_width(theme.fonts.body, item->label) <=
                      theme.metrics.screen_width - 2 * theme.metrics.padding);
                CHECK(item->value(item->context) != NULL);
                ScreenAction action = item->select(item->context);
                CHECK(action.target != NULL);
            }
            store.settings.status_items = 0xD0;
            DebugSpan title = {i18n_text(STR_STATUS_VOLUME), 0};
            title.length = strlen(title.text);
            DebugSpan option = {i18n_text(STR_ON), 0};
            option.length = strlen(option.text);
            CHECK(choose_setting(&screens, title, option));
            CHECK_EQUAL(0xD0 | SETTINGS_STATUS_VOLUME, store.settings.status_items);
            title = (DebugSpan){i18n_text(STR_STATUS_LAYOUT), 0};
            title.length = strlen(title.text);
            option = (DebugSpan){i18n_text(STR_STATUS_COMPACT), 0};
            option.length = strlen(option.text);
            CHECK(choose_setting(&screens, title, option));
            CHECK(store.settings.status_bar_compact);
            CHECK_EQUAL(0xD0 | SETTINGS_STATUS_VOLUME, store.settings.status_items);
            option = (DebugSpan){i18n_text(STR_STATUS_STANDARD), 0};
            option.length = strlen(option.text);
            CHECK(choose_setting(&screens, title, option));
            CHECK(!store.settings.status_bar_compact);
        }
    }
}

static void open_default_buttons(TextSize size)
{
    open_controls(size, ORIENTATION_LANDSCAPE);
    store.settings = settings_defaults();
    buttons.menu.screen.enter(&buttons.menu, 0);
}

static void default_button_values_fit_beside_their_labels_in_landscape(void)
{
    for (int size = TEXT_SIZE_SMALL; size <= TEXT_SIZE_MEDIUM; size++) {
        open_default_buttons((TextSize)size);
        CHECK_EQUAL(LIST_LINES_ONE, buttons.menu.list.lines);
        CHECK_EQUAL(theme_visible_rows(&theme), buttons.menu.list.visible_rows);
    }
}

static void buttons_name_the_effective_action_and_mark_only_changed_ones_custom(void)
{
    static const StringId defaults[PLAYBACK_BUTTON_COUNT] = {
        STR_BUTTON_VOLUME_UP,   STR_BUTTON_KEEP_RAISING,
        STR_BUTTON_VOLUME_DOWN, STR_BUTTON_KEEP_LOWERING,
        STR_BUTTON_PLAY_PAUSE,  STR_BUTTON_NO_ACTION,
        STR_QUICK_SETTINGS,     STR_LYRICS};
    open_default_buttons(TEXT_SIZE_MEDIUM);
    store.settings.button_actions[PLAYBACK_VOLUME_UP_HOLD] = BUTTON_ACTION_NEXT;
    MenuScreen *menu = &buttons.menu;
    CHECK_EQUAL(BUTTON_ROW_FIRST, menu->list.selected);
    CHECK_EQUAL(LIST_ROW_SECTION, menu->model.role(menu, 0));
    CHECK(strcmp(i18n_text(STR_NOW_PLAYING), menu->model.label(menu, 0)) == 0);
    for (int button = 0; button < PLAYBACK_BUTTON_COUNT; button++) {
        int row = BUTTON_ROW_FIRST + button;
        bool custom = button == PLAYBACK_VOLUME_UP_HOLD;
        StringId shown = custom ? STR_BUTTON_NEXT : defaults[button];
        CHECK(strcmp(i18n_text(shown), menu->model.detail(menu, row)) == 0);
        const char *note = menu->model.note(menu, row);
        CHECK(custom ? note != NULL && strcmp(i18n_text(STR_BUTTON_CUSTOM), note) == 0
                     : note == NULL);
        CHECK(menu->model.icon(menu, row) != ICON_NONE);
    }
    CHECK(strcmp(i18n_text(STR_BUTTON_SKIP_TRACK), menu->model.detail(menu, BUTTON_ROW_SKIP)) == 0);
    CHECK(strcmp(i18n_text(STR_BUTTON_SEEK), menu->model.detail(menu, BUTTON_ROW_SEEK)) == 0);
    CHECK(!menu->model.opens(menu, BUTTON_ROW_SKIP));
    CHECK_EQUAL(LIST_ROW_SECTION, menu->model.role(menu, BUTTON_ROW_EVERY_SCREEN));
    CHECK(strcmp(i18n_text(STR_LONG_CURRENT), menu->model.detail(menu, BUTTON_ROW_LONG)) == 0);
    CHECK(menu->model.note(menu, BUTTON_ROW_LONG) == NULL);
}

static void the_chord_rows_only_explain_and_sections_are_skipped(void)
{
    open_default_buttons(TEXT_SIZE_MEDIUM);
    MenuScreen *menu = &buttons.menu;
    list_view_select(&menu->list, BUTTON_ROW_SKIP, 0);
    KeyEvent play = {.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS};
    CHECK_EQUAL(SCREEN_ACTION_NONE, menu->screen.handle_key(menu, play, 0).type);
    KeyEvent next = {.key = KEY_VOLUME_UP, .gesture = KEY_GESTURE_PRESS};
    menu->screen.handle_key(menu, next, 0);
    menu->screen.handle_key(menu, next, 0);
    CHECK_EQUAL(BUTTON_ROW_LONG, menu->list.selected);
}

static void control_choices_validate_names_and_report_each_change(void)
{
    open_controls(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    changed_count = 0;
    buttons.config.changed = settings_changed;
    buttons.config.context = &changed_count;

    CHECK(choose_control(span_of(i18n_text(STR_MENU_LONG_ACTION)),
                         span_of(i18n_text(STR_SLEEP_TIMER))));
    CHECK_EQUAL(5, store.settings.long_press_action);
    CHECK_EQUAL(1, changed_count);

    CHECK(choose_control(span_of(i18n_text(STR_BUTTON_VOLUME_UP_TAP)),
                         span_of(i18n_text(STR_BUTTON_HOME))));
    CHECK_EQUAL(BUTTON_ACTION_HOME, store.settings.button_actions[PLAYBACK_VOLUME_UP_TAP]);
    CHECK_EQUAL(2, changed_count);

    CHECK(!choose_control(span_of(i18n_text(STR_MENU_LONG_ACTION)), span_of("Not an action")));
    CHECK(!choose_control(span_of("Not a button"), span_of(i18n_text(STR_BUTTON_HOME))));
    CHECK_EQUAL(2, changed_count);
}

static void control_menu_recovers_invalid_values_and_bounds_choice_callbacks(void)
{
    open_controls(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    changed_count = 0;
    buttons.config.changed = settings_changed;
    buttons.config.context = &changed_count;

    store.settings.button_actions[PLAYBACK_VOLUME_UP_TAP] = UINT8_MAX;
    const MenuItem *volume_up = &buttons.items[BUTTON_ROW_FIRST + PLAYBACK_VOLUME_UP_TAP];
    CHECK(strcmp(volume_up->value(volume_up->context), i18n_text(STR_BUTTON_VOLUME_UP)) == 0);
    ScreenAction action = volume_up->select(volume_up->context);
    CHECK(action.target == &buttons.choice.screen);
    CHECK_EQUAL(UINT8_MAX, buttons.choice.config.current(buttons.choice.config.context));
    buttons.choice.config.choose(buttons.choice.config.context, -1);
    buttons.choice.config.choose(buttons.choice.config.context, BUTTON_ACTION_COUNT);
    CHECK_EQUAL(0, changed_count);
    buttons.choice.config.choose(buttons.choice.config.context, BUTTON_ACTION_NEXT);
    CHECK_EQUAL(BUTTON_ACTION_NEXT, store.settings.button_actions[PLAYBACK_VOLUME_UP_TAP]);
    CHECK_EQUAL(1, changed_count);

    store.settings.long_press_action = UINT8_MAX;
    const MenuItem *long_row = &buttons.items[BUTTON_ROW_LONG];
    CHECK(strcmp(long_row->value(long_row->context), i18n_text(STR_LONG_CURRENT)) == 0);
    action = long_row->select(long_row->context);
    CHECK(action.target == &buttons.choice.screen);
    buttons.choice.config.choose(buttons.choice.config.context, -1);
    buttons.choice.config.choose(buttons.choice.config.context, 6);
    CHECK_EQUAL(1, changed_count);
    buttons.choice.config.choose(buttons.choice.config.context, 1);
    CHECK_EQUAL(1, store.settings.long_press_action);
    CHECK_EQUAL(2, changed_count);

    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        store.settings.button_actions[index] = UINT8_MAX;
    }
    store.settings.long_press_action = UINT8_MAX;
    action = buttons.items[BUTTON_ROW_RESET].select(buttons.items[BUTTON_ROW_RESET].context);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK_EQUAL(0, store.settings.long_press_action);
    for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
        CHECK_EQUAL(settings_default_button_action((PlaybackButton)index),
                    store.settings.button_actions[index]);
    }
    CHECK_EQUAL(3, changed_count);
}

static void debug_choices_handle_dynamic_options_and_reject_unknown_text(void)
{
    open_controls(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    static SettingsScreens screens;
    static Player player;
    static Library library;
    static PowerPolicy power;
    static Toast toast;
    static BluetoothLink link;
    static BluetoothScreen bluetooth;
    player_init(&player, &library);
    bluetooth_screen_init(&bluetooth, &link, &theme);
    SettingsContext context = {.store = &store,
                               .player = &player,
                               .power = &power,
                               .toast = &toast,
                               .bluetooth = &bluetooth,
                               .theme = &theme};
    settings_screens_init(&screens, context, &theme);

    CHECK(choose_setting(&screens, span_of(i18n_text(STR_BRIGHTNESS)), span_of("5")));
    CHECK_EQUAL(SETTINGS_BRIGHTNESS_MIN + 4, store.settings.brightness);
    uint8_t brightness = store.settings.brightness;
    uint8_t status_items = store.settings.status_items;
    CHECK(!choose_setting(&screens, span_of(i18n_text(STR_BRIGHTNESS)), span_of("Not a level")));
    CHECK(
        !choose_setting(&screens, span_of(i18n_text(STR_STATUS_VOLUME)), span_of("Not an option")));
    CHECK(!choose_setting(&screens, span_of("Not a setting"), span_of(i18n_text(STR_ON))));
    CHECK_EQUAL(brightness, store.settings.brightness);
    CHECK_EQUAL(status_items, store.settings.status_items);
}

static SettingsScreens grouped;

static void open_grouped(void)
{
    static Player player;
    static Library library;
    static PowerPolicy power;
    static Toast toast;
    static BluetoothLink link;
    static BluetoothScreen bluetooth;
    open_controls(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    player_init(&player, &library);
    bluetooth_screen_init(&bluetooth, &link, &theme);
    SettingsContext context = {.store = &store,
                               .player = &player,
                               .power = &power,
                               .toast = &toast,
                               .bluetooth = &bluetooth,
                               .theme = &theme};
    Screen *screen = settings_screens_init(&grouped, context, &theme);
    screen->enter(screen->context, 0);
}

static void settings_open_on_nine_groups_with_language_fourth_and_about_last(void)
{
    static const StringId order[SETTINGS_GROUP_COUNT] = {
        STR_SETTINGS_SOUND,   STR_SETTINGS_PLAYBACK,      STR_SETTINGS_DISPLAY_TEXT,
        STR_LANGUAGE,         STR_SETTINGS_POWER_BATTERY, STR_SETTINGS_CONNECTIONS,
        STR_SETTINGS_BUTTONS, STR_SETTINGS_LIBRARY,       STR_ABOUT};
    open_grouped();
    MenuScreen *menu = &grouped.menu;
    CHECK_EQUAL(SETTINGS_GROUP_COUNT, menu->model.count(menu));
    for (int row = 0; row < SETTINGS_GROUP_COUNT; row++) {
        CHECK(strcmp(i18n_text(order[row]), menu->model.label(menu, row)) == 0);
        CHECK(menu->model.icon(menu, row) != ICON_NONE);
    }
    CHECK(grouped.languages.count <= 1);
    CHECK_EQUAL(ICON_GLOBE, menu->model.icon(menu, SETTINGS_GROUP_LANGUAGE));
    CHECK(strcmp(i18n_text(STR_LANGUAGE_NAME), menu->model.detail(menu, SETTINGS_GROUP_LANGUAGE)) ==
          0);
    CHECK(strcmp(i18n_text((StringId)(STR_SIZE_SMALL + store.settings.text_size)),
                 menu->model.detail(menu, SETTINGS_GROUP_DISPLAY)) == 0);
    CHECK(menu->model.detail(menu, SETTINGS_GROUP_SOUND) == NULL);
}

static int times_offered(const char *label)
{
    int times = 0;
    for (int group = 0; group < SETTINGS_GROUP_COUNT; group++) {
        const MenuItem *row = &grouped.groups[group];
        ScreenAction opened =
            row->select != NULL ? row->select(row->context) : screen_action_none();
        if (opened.target != &grouped.group_menu.screen) {
            times += strcmp(label, row->label) == 0 ? 1 : 0;
            continue;
        }
        for (int index = 0; index < grouped.group_menu.config.item_count; index++) {
            times += strcmp(label, grouped.group_menu.config.items[index].label) == 0 ? 1 : 0;
        }
    }
    return times;
}

static void every_former_settings_row_is_in_exactly_one_group(void)
{
    static const StringId rows[] = {STR_SHUFFLE,
                                    STR_REPEAT,
                                    STR_BLUETOOTH,
                                    STR_SLEEP_TIMER,
                                    STR_AUTO_OFF,
                                    STR_SCREEN_TIMEOUT,
                                    STR_CLOCK_FORMAT,
                                    STR_SET_CLOCK,
                                    STR_BRIGHTNESS,
                                    STR_COLOR,
                                    STR_TEXT_SIZE,
                                    STR_ORIENTATION,
                                    STR_LANGUAGE,
                                    STR_GAIN,
                                    STR_DAC_FILTER,
                                    STR_OUTPUT,
                                    STR_EQUALIZER,
                                    STR_CUSTOM_EQ,
                                    STR_REPLAY_GAIN,
                                    STR_PREVENT_CLIPPING,
                                    STR_START_SCREEN,
                                    STR_RESCAN_LIBRARY,
                                    STR_HIDDEN_TRACKS,
                                    STR_USB_MODE,
                                    STR_THEME,
                                    STR_STATUS_BAR,
                                    STR_PLAYBACK_SPEED,
                                    STR_REWIND_ON_RESUME,
                                    STR_SAFE_START_VOLUME,
                                    STR_TRANSITIONS,
                                    STR_TRANSITION_LENGTH,
                                    STR_USB_ANSWER,
                                    STR_POWER_ON,
                                    STR_SKIP_SILENCE,
                                    STR_SPEECH_BOOST,
                                    STR_CROSSFEED,
                                    STR_HEADPHONES,
                                    STR_CARD_EJECT,
                                    STR_CARD_MOUNT,
                                    STR_CARD_FORMAT,
                                    STR_HOME_SCREEN,
                                    STR_PEQ,
                                    STR_STANDBY_POWER,
                                    STR_NOW_PLAYING_STYLE,
                                    STR_BACKUPS,
                                    STR_SETTINGS_BUTTONS,
                                    STR_ABOUT};
    open_grouped();
    for (size_t index = 0; index < sizeof rows / sizeof rows[0]; index++) {
        CHECK_EQUAL(1, times_offered(i18n_text(rows[index])));
    }
    int listed = 0;
    for (int group = 0; group < SETTINGS_GROUP_COUNT; group++) {
        const MenuItem *row = &grouped.groups[group];
        ScreenAction opened =
            row->select != NULL ? row->select(row->context) : screen_action_none();
        listed +=
            opened.target == &grouped.group_menu.screen ? grouped.group_menu.config.item_count : 1;
    }
    CHECK_EQUAL((int)(sizeof rows / sizeof rows[0]), listed);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(settings_open_on_nine_groups_with_language_fourth_and_about_last),
        TEST_CASE(every_former_settings_row_is_in_exactly_one_group),
        TEST_CASE(status_bar_choices_fit_every_size_and_orientation_and_save_independently),
        TEST_CASE(value_rows_get_a_separate_line_in_large_portrait),
        TEST_CASE(every_control_label_and_value_is_shown_whole_in_portrait),
        TEST_CASE(default_button_values_fit_beside_their_labels_in_landscape),
        TEST_CASE(buttons_name_the_effective_action_and_mark_only_changed_ones_custom),
        TEST_CASE(the_chord_rows_only_explain_and_sections_are_skipped),
        TEST_CASE(settings_values_use_the_same_whole_label_layout_in_large_portrait),
        TEST_CASE(control_choices_validate_names_and_report_each_change),
        TEST_CASE(control_menu_recovers_invalid_values_and_bounds_choice_callbacks),
        TEST_CASE(debug_choices_handle_dynamic_options_and_reject_unknown_text),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
