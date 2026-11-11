#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "screen_snapshot.h"
#include "settings_screens.h"
#include "ui/header.h"
#include "ui/label.h"

enum {
    PROMPT_MS = 15000,
    LINE_ROWS = 3,
    LARGEST_SIDE = 480,
    PLACES = 3,
    SETTINGS_USB_ANSWER_ITEM = SETTINGS_CHOICE_COUNT + 4
};

static Color pixels[LARGEST_SIDE * LARGEST_SIDE];

static const HalBattery ON_A_CABLE = {
    .known = true, .percent = 80, .usb_power = true, .charging = true, .millivolts = 4000};

static SettingsScreens screens;
static SettingsStore store;
static Player player;
static Toast toast;
static Theme theme;

static void start(HalBattery battery)
{
    fake_hal_reset();
    fake_hal_set_battery(battery);
    i18n_detach();
    theme = BOARD_THEME;
    store.settings = settings_defaults();
    toast = (Toast){0};
    SettingsContext context = {.store = &store, .player = &player, .toast = &toast};
    settings_screens_init(&screens, context, &theme);
}

static Screen *open_chooser(bool prompt)
{
    start(ON_A_CABLE);
    Screen *screen = settings_screens_usb_modes(&screens, prompt);
    screen->enter(screen->context, 0);
    return screen;
}

static const char *label(int row)
{
    const PlugChooser *chooser = &screens.usb;
    return chooser->model.label(chooser->model.context, row);
}

static const char *mark(int row)
{
    const PlugChooser *chooser = &screens.usb;
    return chooser->model.detail(chooser->model.context, row);
}

static ScreenAction key(Screen *screen, Key pressed, KeyGesture gesture, uint32_t now_ms)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = pressed, .gesture = gesture},
                              now_ms);
}

static ScreenAction press(Screen *screen, Key pressed, uint32_t now_ms)
{
    return key(screen, pressed, KEY_GESTURE_PRESS, now_ms);
}

static void the_prompt_offers_connect_to_computer_first_then_charge_usb_power_and_the_dac(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("Computer found", screen->title) == 0);
    CHECK(strcmp("Connect to computer", label(0)) == 0);
    CHECK(strcmp("Charge", label(1)) == 0);
    CHECK(strcmp("Play on USB power", label(2)) == 0);
    CHECK(strcmp("USB DAC", label(3)) == 0);
    CHECK_EQUAL(0, screens.usb.list.selected);
    CHECK(mark(0) == NULL);
    CHECK(mark(1) != NULL);
}

static void the_highlighted_mode_explains_what_happens_to_the_music(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("Copy music, update, use the app. Music stops.",
                 plug_chooser_line(&screens.usb)) == 0);
    press(screen, KEY_VOLUME_UP, 100);
    CHECK(strcmp("Keep listening while charging.", plug_chooser_line(&screens.usb)) == 0);
}

static void the_prompt_counts_down_to_charging(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("Charging in 15 s", screens.usb.countdown) == 0);
    screen->update(screen->context, 10000);
    CHECK(strcmp("Charging in 5 s", screens.usb.countdown) == 0);
    CHECK(screens.usb.redraw);
    CHECK(!screens.usb.expired);
}

static void the_prompt_lapses_after_fifteen_quiet_seconds(void)
{
    Screen *screen = open_chooser(true);
    screen->update(screen->context, PROMPT_MS - 1);
    CHECK(!screens.usb.expired);
    screen->update(screen->context, PROMPT_MS);
    CHECK(screens.usb.expired);
}

static void a_key_starts_the_countdown_again(void)
{
    Screen *screen = open_chooser(true);
    press(screen, KEY_VOLUME_UP, 10000);
    CHECK(strcmp("Charging in 15 s", screens.usb.countdown) == 0);
    screen->update(screen->context, PROMPT_MS + 9999);
    CHECK(!screens.usb.expired);
    screen->update(screen->context, PROMPT_MS + 10000);
    CHECK(screens.usb.expired);
}

static void one_press_of_play_connects_to_the_computer(void)
{
    Screen *screen = open_chooser(true);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(screen, KEY_PLAY, 200).type);
    CHECK_EQUAL(1, fake_hal_usb_mode_requests());
    CHECK_EQUAL(HAL_USB_MODE_STORAGE, fake_hal_requested_usb_mode());
    CHECK_EQUAL(USB_CONNECT_ASK, store.settings.usb_connect);
}

static void play_on_usb_power_holds_charging_off_without_changing_the_saved_answer(void)
{
    Screen *screen = open_chooser(true);
    press(screen, KEY_VOLUME_UP, 100);
    press(screen, KEY_VOLUME_UP, 150);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(screen, KEY_PLAY, 200).type);
    CHECK_EQUAL(PLUG_CARE_USB, fake_hal_battery_care_mode());
    CHECK_EQUAL(HAL_USB_MODE_PLAYER, fake_hal_requested_usb_mode());
    CHECK_EQUAL(PLUG_MODE_ASK, plug_mode_saved(&store.settings));
}

static void holding_play_always_does_this_and_says_so(void)
{
    Screen *screen = open_chooser(true);
    press(screen, KEY_VOLUME_DOWN, 100);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(screen, KEY_PLAY, KEY_GESTURE_LONG_PRESS, 200).type);
    CHECK_EQUAL(HAL_USB_MODE_DAC, fake_hal_requested_usb_mode());
    CHECK_EQUAL(PLUG_MODE_DAC, plug_mode_saved(&store.settings));
    CHECK_EQUAL(HAL_USB_ANSWER_DAC, fake_hal_usb_answer());
    CHECK(strcmp("Always: USB DAC", toast.text) == 0);
}

static void back_leaves_the_prompt_without_a_request(void)
{
    Screen *screen = open_chooser(true);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(screen, KEY_BACK, 100).type);
    CHECK_EQUAL(0, fake_hal_usb_mode_requests());
    CHECK_EQUAL(PLUG_CARE_FULL, fake_hal_battery_care_mode());
}

static void usb_mode_opened_by_hand_neither_counts_down_nor_lapses(void)
{
    Screen *screen = open_chooser(false);
    CHECK(strcmp("USB mode", screen->title) == 0);
    CHECK_EQUAL(1, screens.usb.list.selected);
    screen->update(screen->context, 600000);
    CHECK(!screens.usb.expired);
    CHECK_EQUAL(1, plug_chooser_footer_rows(&screens.usb));
}

static void usb_mode_marks_play_on_usb_power_while_charging_is_held_off(void)
{
    start(ON_A_CABLE);
    hal_power_set_battery_care(PLUG_CARE_USB);
    Screen *screen = settings_screens_usb_modes(&screens, false);
    screen->enter(screen->context, 0);
    CHECK_EQUAL(2, screens.usb.list.selected);
    CHECK(mark(2) != NULL);
    CHECK(mark(1) == NULL);
}

static void use_theme(int appearance, int orientation, int size)
{
    theme = BOARD_THEME;
    default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
}

static Screen *open_at(int place)
{
    if (place == PLUG_PLACE_SETTINGS) {
        const MenuItem *item = &screens.items[SETTINGS_USB_ANSWER_ITEM];
        Screen *answer = item->select(item->context).target;
        answer->enter(answer->context, 0);
        return answer;
    }
    Screen *screen = settings_screens_usb_modes(&screens, place == PLUG_PLACE_PROMPT);
    screen->enter(screen->context, 0);
    return screen;
}

static const PlugChooser *chooser_at(int place)
{
    return place == PLUG_PLACE_SETTINGS ? &screens.plug : &screens.usb;
}

static void every_line_fits_its_three_rows(const PlugChooser *chooser)
{
    PlugChooserLayout layout =
        plug_chooser_layout(&theme, plug_chooser_footer_rows(chooser), chooser->choices.count);
    for (int row = 0; row < chooser->choices.count; row++) {
        PlugMode mode = plug_chooser_mode(chooser, row);
        const char *line = i18n_text(plug_mode_line(mode, ON_A_CABLE));
        CHECK(label_wrap_count(theme.fonts.small, line, layout.line.width) <= LINE_ROWS);
    }
    int hint = text_width(theme.fonts.small, i18n_text(STR_PLUG_ALWAYS_HINT));
    int count = text_width(theme.fonts.small, chooser->countdown);
    CHECK((theme_is_portrait(&theme) ? (hint > count ? hint : count) : hint + count) <=
          layout.footer.width);
    CHECK(layout.name.height == 0 ||
          label_wrap_count(theme.fonts.title, i18n_text(plug_mode_name(PLUG_MODE_COMPUTER)),
                           layout.name.width) <= 2);
    Rect title = header_title_bounds(&theme);
    CHECK(text_width(theme.fonts.header, chooser->screen.title) <= title.width);
}

static void draw_variant(Screen *screen, const char *name, int variant)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    screen->draw(screen->context, &canvas);
    screen_snapshot_write(&canvas, name, variant);
}

static void every_chooser_draws_at_every_theme_size_and_orientation(void)
{
    static const char *const NAMES[PLACES] = {"plug-prompt", "plug-usb-mode", "plug-settings"};
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                for (int place = 0; place < PLACES; place++) {
                    start(ON_A_CABLE);
                    use_theme(appearance, orientation, size);
                    Screen *screen = open_at(place);
                    every_line_fits_its_three_rows(chooser_at(place));
                    draw_variant(screen, NAMES[place], appearance * 100 + orientation * 10 + size);
                }
            }
        }
    }
}

static void each_mode_has_its_own_figure(void)
{
    start(ON_A_CABLE);
    use_theme(APPEARANCE_NEON_NOIR, ORIENTATION_LANDSCAPE, TEXT_SIZE_MEDIUM);
    Screen *screen = open_at(PLUG_PLACE_SETTINGS);
    press(screen, KEY_VOLUME_UP, 10);
    for (int row = 0; row < PLUG_MODE_COUNT; row++) {
        list_view_select(&screens.plug.list, row, 0);
        draw_variant(screen, "plug-figure", row);
    }
}

static void a_low_battery_shows_why_usb_power_charges_first(void)
{
    start((HalBattery){.known = true, .percent = 9, .usb_power = true, .millivolts = 3620});
    Screen *screen = settings_screens_usb_modes(&screens, true);
    screen->enter(screen->context, 0);
    press(screen, KEY_VOLUME_UP, 100);
    press(screen, KEY_VOLUME_UP, 150);
    CHECK(strcmp("Battery low: charges first", plug_chooser_line(&screens.usb)) == 0);
    draw_variant(screen, "plug-low-battery", 0);
}

static void every_chooser_fits_every_size_and_orientation(void)
{
    start(ON_A_CABLE);
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int turn = 0; turn < 2; turn++) {
            theme = BOARD_THEME;
            default_theme_set_orientation(&theme,
                                          turn == 0 ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT);
            default_theme_set_text_size(&theme, (TextSize)size);
            for (int rows = 0; rows <= 2; rows++) {
                PlugChooserLayout layout = plug_chooser_layout(&theme, rows, PLUG_MODE_COUNT);
                Rect screen = {0, 0, theme.metrics.screen_width, theme.metrics.screen_height};
                CHECK(layout.art.height >= theme.metrics.row_height);
                CHECK(layout.art.width >= theme.metrics.row_height);
                CHECK(layout.list.height >= 4 * theme.metrics.row_height);
                CHECK(layout.list.y >= layout.line.y + layout.line.height ||
                      layout.list.x >= layout.line.x + layout.line.width);
                CHECK(layout.line.y >= layout.art.y + layout.art.height);
                CHECK(layout.footer.y + layout.footer.height <= screen.height);
                CHECK(layout.line.x + layout.line.width <= screen.width);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_prompt_offers_connect_to_computer_first_then_charge_usb_power_and_the_dac),
        TEST_CASE(the_highlighted_mode_explains_what_happens_to_the_music),
        TEST_CASE(the_prompt_counts_down_to_charging),
        TEST_CASE(the_prompt_lapses_after_fifteen_quiet_seconds),
        TEST_CASE(a_key_starts_the_countdown_again),
        TEST_CASE(one_press_of_play_connects_to_the_computer),
        TEST_CASE(play_on_usb_power_holds_charging_off_without_changing_the_saved_answer),
        TEST_CASE(holding_play_always_does_this_and_says_so),
        TEST_CASE(back_leaves_the_prompt_without_a_request),
        TEST_CASE(usb_mode_opened_by_hand_neither_counts_down_nor_lapses),
        TEST_CASE(usb_mode_marks_play_on_usb_power_while_charging_is_held_off),
        TEST_CASE(a_low_battery_shows_why_usb_power_charges_first),
        TEST_CASE(every_chooser_fits_every_size_and_orientation),
        TEST_CASE(every_chooser_draws_at_every_theme_size_and_orientation),
        TEST_CASE(each_mode_has_its_own_figure),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
