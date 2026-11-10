#include <string.h>

#include "button_settings.h"
#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "settings_screens.h"
#include "ui/label.h"

static Color pixels[HAL_DISPLAY_SHORT_SIDE * HAL_DISPLAY_LONG_SIDE];
static Color expected[HAL_DISPLAY_SHORT_SIDE * 80];
static Theme theme;
static SettingsStore store;
static ButtonSettings buttons;

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

static void compare_text(const Canvas *actual, Rect area, const Font *font, const char *text)
{
    int width = text_width(font, text);
    CHECK(width <= area.width);
    Canvas reference =
        canvas_make(expected, (Rect){0, area.y, HAL_DISPLAY_SHORT_SIDE, area.height});
    canvas_clear(&reference, theme.colors.selection_background);
    label_draw(&reference, (TextStyle){.font = font, .color = theme.colors.selection_text},
               (Label){area, text});
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
        for (int index = 0; index < PLAYBACK_BUTTON_COUNT; index++) {
            list_view_select(&menu->list, index, 0);
            menu->screen.update(menu, 0);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            menu->screen.draw(menu, &canvas);
            int y = theme.metrics.header_height + (index - menu->list.top) * row_height;
            int inset = theme.metrics.padding;
            int width = theme.metrics.screen_width - 2 * inset;
            if (menu->model.count(menu) > menu->list.visible_rows) {
                width -= theme.metrics.scrollbar_width + theme.metrics.scrollbar_inset;
            }
            const char *label = menu->model.label(menu, index);
            const char *detail = menu->model.detail(menu, index);
            compare_text(&canvas, (Rect){inset, y, width, theme.metrics.row_height},
                         theme.fonts.body, label);
            compare_text(&canvas,
                         (Rect){inset, y + theme.metrics.row_height + 2, width - 24,
                                font_line_height(theme.fonts.small)},
                         theme.fonts.small, detail);
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
    Screen *screen = settings_screens_init(&screens, context, &theme);
    screens.items[SETTINGS_ABOUT_ITEM].label = i18n_text(STR_ABOUT);
    screen->enter(screen->context, 0);
    MenuScreen *menu = &screens.menu;
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
    compare_text(&canvas, (Rect){inset, y, width, theme.metrics.row_height}, theme.fonts.body,
                 i18n_text(STR_SCREEN_TIMEOUT));
    compare_text(&canvas,
                 (Rect){inset, y + theme.metrics.row_height + 2, width - 24,
                        font_line_height(theme.fonts.small)},
                 theme.fonts.small, menu->model.detail(menu, index));
}

static void landscape_controls_keep_the_original_row_geometry(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        open_controls((TextSize)size, ORIENTATION_LANDSCAPE);
        CHECK_EQUAL(theme_visible_rows(&theme), buttons.menu.list.visible_rows);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(value_rows_get_a_separate_line_in_large_portrait),
        TEST_CASE(every_control_label_and_value_is_shown_whole_in_portrait),
        TEST_CASE(landscape_controls_keep_the_original_row_geometry),
        TEST_CASE(settings_values_use_the_same_whole_label_layout_in_large_portrait),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
