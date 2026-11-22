#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "screen_snapshot.h"
#include "settings_screens.h"
#include "test_pack.h"
#include "text_spy.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/status_bar.h"

enum {
    PROMPT_MS = 15000,
    LARGEST_SIDE = 480,
    PLACES = 3
};

static Color pixels[LARGEST_SIDE * LARGEST_SIDE];

static const HalBattery ON_A_CABLE = {
    .known = true, .percent = 80, .usb_power = true, .charging = true, .millivolts = 4000};

static SettingsScreens screens;
static SettingsStore store;
static Player player;
static Toast toast;
static Theme theme;

static void start_in(HalBattery battery, const uint8_t *pack, uint32_t size)
{
    fake_hal_reset();
    fake_hal_set_battery(battery);
    i18n_detach();
    if (pack != NULL) {
        CHECK(i18n_attach(pack, size));
        uint32_t segment = test_pack_segment_of(STR_PLUG_PAUSED);
        I18nSegment row = test_pack_segment(pack, segment);
        CHECK(segment == 0 || i18n_attach_segment(segment, pack + row.offset, &row));
    }
    theme = BOARD_THEME;
    store.settings = settings_defaults();
    toast = (Toast){0};
    SettingsContext context = {.store = &store, .player = &player, .toast = &toast};
    settings_screens_init(&screens, context, &theme);
}

static void start(HalBattery battery)
{
    start_in(battery, NULL, 0);
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

static bool mark(int row)
{
    return screens.usb.choices.marked == row;
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
    CHECK(strcmp("Charge & play", label(1)) == 0);
    CHECK(strcmp("Run from USB power", label(2)) == 0);
    CHECK(strcmp("USB audio (DAC)", label(3)) == 0);
    CHECK_EQUAL(0, screens.usb.list.selected);
    for (int row = 0; row < 4; row++) {
        CHECK(!mark(row));
    }
}

static void the_highlighted_mode_explains_what_happens_to_the_music(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("Music drives, firmware updates, companion app.",
                 plug_chooser_line(&screens.usb)) == 0);
    press(screen, KEY_VOLUME_UP, 100);
    CHECK(strcmp("Charges the battery while you keep listening.",
                 plug_chooser_line(&screens.usb)) == 0);
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

static void the_countdown_shows_in_the_header_without_the_charging_bolt(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("Charging in 15 s", screen->header_status) == 0);
    CHECK(screen->hides_charging);
    Screen *modes = open_chooser(false);
    CHECK(modes->header_status == NULL);
    CHECK(!modes->hides_charging);
}

static void the_first_volume_press_pauses_auto_charge(void)
{
    Screen *screen = open_chooser(true);
    screens.usb.redraw = false;
    press(screen, KEY_VOLUME_UP, 10000);
    CHECK(screens.usb.paused);
    CHECK(screens.usb.redraw);
    CHECK(strcmp("Auto-charge paused", screen->header_status) == 0);
    CHECK_EQUAL(1, screens.usb.list.selected);
    screen->update(screen->context, 600000);
    CHECK(!screens.usb.expired);
    CHECK(strcmp("Auto-charge paused", screen->header_status) == 0);
}

static void other_keys_leave_the_countdown_running(void)
{
    Screen *screen = open_chooser(true);
    key(screen, KEY_MENU, KEY_GESTURE_PRESS, 10000);
    CHECK(!screens.usb.paused);
    screen->update(screen->context, PROMPT_MS);
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
    CHECK(strcmp("Always: USB audio (DAC)", toast.text) == 0);
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
    CHECK(plug_chooser_has_footer(&screens.usb));
}

static void usb_mode_marks_play_on_usb_power_while_charging_is_held_off(void)
{
    start(ON_A_CABLE);
    hal_power_set_battery_care(PLUG_CARE_USB);
    Screen *screen = settings_screens_usb_modes(&screens, false);
    screen->enter(screen->context, 0);
    CHECK_EQUAL(2, screens.usb.list.selected);
    CHECK(mark(2));
    CHECK(!mark(1));
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
        int index = 0;
        while (index + 1 < SETTINGS_ITEM_COUNT &&
               screens.items[index].label != i18n_text(STR_USB_ANSWER)) {
            index++;
        }
        const MenuItem *item = &screens.items[index];
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

static void every_line_fits_its_rows(const PlugChooser *chooser)
{
    PlugChooserLayout computed = plug_chooser_layout(chooser);
    const PlugChooserLayout *layout = &computed;
    int rows = theme_is_portrait(&theme) ? 3 : 2;
    for (int row = 0; row < chooser->choices.count; row++) {
        PlugMode mode = plug_chooser_mode(chooser, row);
        const char *line = i18n_text(plug_mode_line(mode, ON_A_CABLE));
        CHECK(label_wrap_count(theme.fonts.small, line, layout->line.width) <= rows);
        CHECK(chooser->heights[row] >= theme.metrics.row_height);
    }
    CHECK(rows * font_line_height(theme.fonts.small) <= layout->line.height);
    static const FooterHint ALWAYS[] = {{FOOTER_KEY_HOLD_PLAY, STR_HINT_ALWAYS}};
    FooterHints footer = {ALWAYS, 1, FOOTER_ALIGN_END};
    CHECK(!plug_chooser_has_footer(chooser) ||
          footer_hints_lines(&theme, footer, layout->footer.width - 2 * theme.metrics.padding) ==
              1);
    CHECK(layout->title.height == 0 ||
          label_wrap_count(theme.fonts.header, chooser->screen.title, layout->title.width) <= 2);
    Rect title = header_title_bounds(&theme);
    CHECK(text_width(theme.fonts.header, chooser->screen.title) <= title.width);
    CHECK(plug_chooser_shown_rows(chooser) >= 1);
}

static void the_header_status_is_laid_out(const PlugChooser *chooser, const char *status)
{
    StatusBarState bar = {.battery_known = true,
                          .battery_percent = 80,
                          .usb_power = true,
                          .hides_charging = true,
                          .position = status};
    bar.title_width =
        chooser->screen.untitled_header ? 0 : text_width(theme.fonts.header, chooser->screen.title);
    StatusBarLayout layout;
    status_bar_layout(&theme, &bar, &layout);
    CHECK((layout.items & STATUS_BAR_POSITION) != 0);
}

static void every_header_status_fits(const PlugChooser *chooser)
{
    if (!chooser->timed) {
        return;
    }
    the_header_status_is_laid_out(chooser, chooser->countdown);
    if (!chooser->screen.untitled_header) {
        the_header_status_is_laid_out(chooser, i18n_text(STR_PLUG_PAUSED));
    }
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
                    every_line_fits_its_rows(chooser_at(place));
                    every_header_status_fits(chooser_at(place));
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

static void the_layout_stays_on_the_screen(const PlugChooser *chooser)
{
    PlugChooserLayout computed = plug_chooser_layout(chooser);
    const PlugChooserLayout *layout = &computed;
    Rect screen = {0, 0, theme.metrics.screen_width, theme.metrics.screen_height};
    CHECK(layout->art.height >= theme.metrics.row_height);
    CHECK(layout->art.width >= theme.metrics.row_height);
    CHECK(layout->list.y >= layout->art.y + layout->art.height ||
          layout->list.x >= layout->art.x + layout->art.width);
    CHECK(layout->line.y >= layout->list.y + layout->list.height);
    CHECK(layout->line.y + layout->line.height <= layout->footer.y);
    CHECK(layout->cue.height == 0 || layout->cue.y + layout->cue.height <= layout->footer.y);
    CHECK(layout->footer.y + layout->footer.height <= screen.height);
    CHECK(layout->line.x + layout->line.width <= screen.width);
}

static void every_chooser_fits_every_size_and_orientation(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int turn = 0; turn < 2; turn++) {
            for (int place = 0; place < PLACES; place++) {
                start(ON_A_CABLE);
                use_theme(0, turn == 0 ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT, size);
                Screen *screen = open_at(place);
                const PlugChooser *chooser = chooser_at(place);
                the_layout_stays_on_the_screen(chooser);
                for (int row = 0; row < chooser->choices.count; row++) {
                    press(screen, KEY_VOLUME_DOWN, 100 + (uint32_t)row);
                    CHECK(chooser->list.selected >= chooser->list.top);
                    CHECK(chooser->list.selected <
                          chooser->list.top + plug_chooser_shown_rows(chooser));
                }
            }
        }
    }
}

static void every_choice_keeps_its_full_name_in_both_orientations(void)
{
    static const char *const FULL[] = {"Connect to computer", "Charge & play", "Run from USB power",
                                       "USB audio (DAC)"};
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int turn = 0; turn < 2; turn++) {
            start(ON_A_CABLE);
            use_theme(0, turn == 0 ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT, size);
            open_at(PLUG_PLACE_PROMPT);
            for (int row = 0; row < 4; row++) {
                CHECK(strcmp(FULL[row], label(row)) == 0);
            }
        }
    }
}

static void medium_landscape_shows_all_four_choices_and_the_explanation(void)
{
    for (int place = PLUG_PLACE_PROMPT; place <= PLUG_PLACE_SWITCH; place++) {
        start(ON_A_CABLE);
        use_theme(0, ORIENTATION_LANDSCAPE, TEXT_SIZE_MEDIUM);
        open_at(place);
        const PlugChooser *chooser = chooser_at(place);
        CHECK_EQUAL(chooser->choices.count, plug_chooser_shown_rows(chooser));
        CHECK_EQUAL(0, chooser->cue_area.height);
        CHECK(2 * font_line_height(theme.fonts.small) <= plug_chooser_layout(chooser).line.height);
    }
}

static void the_portrait_prompt_moves_its_title_into_the_body(void)
{
    start(ON_A_CABLE);
    use_theme(0, ORIENTATION_PORTRAIT, TEXT_SIZE_MEDIUM);
    Screen *screen = open_at(PLUG_PLACE_PROMPT);
    CHECK(screen->untitled_header);
    CHECK(plug_chooser_layout(&screens.usb).title.height > 0);
    use_theme(0, ORIENTATION_LANDSCAPE, TEXT_SIZE_MEDIUM);
    screen = open_at(PLUG_PLACE_PROMPT);
    CHECK(!screen->untitled_header);
    CHECK_EQUAL(0, plug_chooser_layout(&screens.usb).title.height);
    use_theme(0, ORIENTATION_PORTRAIT, TEXT_SIZE_MEDIUM);
    screen = open_at(PLUG_PLACE_SWITCH);
    CHECK(!screen->untitled_header);
}

static void the_portrait_prompt_says_paused_in_the_body(void)
{
    start(ON_A_CABLE);
    use_theme(0, ORIENTATION_PORTRAIT, TEXT_SIZE_MEDIUM);
    Screen *screen = open_at(PLUG_PLACE_PROMPT);
    press(screen, KEY_VOLUME_DOWN, 100);
    CHECK(screens.usb.paused);
    CHECK(screen->header_status == NULL);
    draw_variant(screen, "plug-paused-portrait", 0);
}

static const char *const PLUG_WORDS[][3] = {
    {"Computer found", "Computer gefunden",
     "\xE3\x82\xB3\xE3\x83\xB3\xE3\x83\x94\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\xBF\xE3\x83\xBC\xE3\x82"
     "\x92\xE6\xA4\x9C\xE5\x87\xBA"},
    {"Auto-charge paused", "Auto-Laden pausiert",
     "\xE8\x87\xAA\xE5\x8B\x95\xE5\x85\x85\xE9\x9B\xBB\xE3\x82\x92\xE4\xB8\x80\xE6\x99\x82\xE5\x81"
     "\x9C\xE6\xAD\xA2"},
    {"Charging in {0} s", "Laden in {0} s",
     "{0}\xE7\xA7\x92\xE5\xBE\x8C\xE3\x81\xAB\xE5\x85\x85\xE9\x9B\xBB"},
    {"Connect to computer", "Mit Computer verbinden",
     "\xE3\x82\xB3\xE3\x83\xB3\xE3\x83\x94\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\xBF\xE3\x83\xBC\xE3\x81"
     "\xAB\xE6\x8E\xA5\xE7\xB6\x9A"},
    {"Charge & play", "Laden & abspielen",
     "\xE5\x85\x85\xE9\x9B\xBB\xE3\x81\x97\xE3\x81\xAA\xE3\x81\x8C\xE3\x82\x89\xE5\x86\x8D\xE7\x94"
     "\x9F"},
    {"Run from USB power",
     "\xC3\x9C"
     "ber USB-Strom betreiben",
     "USB\xE7\xB5\xA6\xE9\x9B\xBB\xE3\x81\xA7\xE5\x8B\x95\xE4\xBD\x9C"}};

static void translate(const char *english, char *text, size_t capacity, int language)
{
    const char *chosen = english;
    for (size_t row = 0; row < sizeof PLUG_WORDS / sizeof PLUG_WORDS[0]; row++) {
        chosen = strcmp(english, PLUG_WORDS[row][0]) == 0 ? PLUG_WORDS[row][language] : chosen;
    }
    size_t length = strlen(chosen) < capacity - 1 ? strlen(chosen) : capacity - 1;
    memcpy(text, chosen, length);
    text[length] = 0;
}

static void german(const char *english, char *text, size_t capacity)
{
    translate(english, text, capacity, 1);
}

static void japanese(const char *english, char *text, size_t capacity)
{
    translate(english, text, capacity, 2);
}

static void japanese_widths(const char *english, char *text, size_t capacity)
{
    const char *chosen = english;
    chosen = strcmp(english, PLUG_WORDS[0][0]) == 0 ? "MMMMMMMMMMMMMM" : chosen;
    chosen = strcmp(english, PLUG_WORDS[1][0]) == 0 ? "MMMM MMMM MMM" : chosen;
    size_t length = strlen(chosen) < capacity - 1 ? strlen(chosen) : capacity - 1;
    memcpy(text, chosen, length);
    text[length] = 0;
}

static bool drawn_in_the_body(const char *text)
{
    char shown[TEXT_SPY_TEXT * 4] = "";
    char wanted[TEXT_SPY_TEXT] = "";
    size_t used = 0;
    for (int index = 0; index < text_spy_count() && used + TEXT_SPY_TEXT < sizeof shown; index++) {
        for (const char *at = text_spy_entry(index)->shown; *at != 0; at++) {
            shown[used] = *at;
            used += *at != ' ' ? 1 : 0;
        }
    }
    shown[used] = 0;
    used = 0;
    for (const char *at = text; *at != 0 && used + 1 < sizeof wanted; at++) {
        wanted[used] = *at;
        used += *at != ' ' ? 1 : 0;
    }
    wanted[used] = 0;
    return strstr(shown, wanted) != NULL;
}

static int paused_prompt_shows_it(int orientation, int size)
{
    use_theme(0, orientation, size);
    Screen *screen = open_at(PLUG_PLACE_PROMPT);
    press(screen, KEY_VOLUME_UP, 100);
    CHECK(screens.usb.paused);
    const char *paused = i18n_text(STR_PLUG_PAUSED);
    if (screen->header_status != NULL) {
        CHECK(strcmp(paused, screen->header_status) == 0);
        the_header_status_is_laid_out(&screens.usb, paused);
        return 0;
    }
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    screen->draw(screen->context, &canvas);
    CHECK(drawn_in_the_body(paused));
    static int shot;
    screen_snapshot_write(&canvas, "plug-paused-body", shot++);
    return 1;
}

static void the_paused_prompt_says_so_in_every_language_and_layout(void)
{
    static uint8_t pack[TEST_PACK_CAPACITY];
    static const TestPackLanguage LANGUAGES[] = {{"en", "English", NULL},
                                                 {"de", "Deutsch", german},
                                                 {"ja", "Japanese", japanese},
                                                 {"jw", "Japanese widths", japanese_widths}};
    int landscape_body = 0;
    for (size_t language = 0; language < sizeof LANGUAGES / sizeof LANGUAGES[0]; language++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                bool english = LANGUAGES[language].translate == NULL;
                uint32_t bytes = english ? 0 : test_pack_build(pack, LANGUAGES[language]);
                start_in(ON_A_CABLE, english ? NULL : pack, bytes);
                int body = paused_prompt_shows_it(orientation, size);
                CHECK(orientation == ORIENTATION_LANDSCAPE || body == 1);
                landscape_body += orientation == ORIENTATION_LANDSCAPE ? body : 0;
            }
        }
    }
    i18n_detach();
    CHECK(landscape_body > 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_prompt_offers_connect_to_computer_first_then_charge_usb_power_and_the_dac),
        TEST_CASE(the_highlighted_mode_explains_what_happens_to_the_music),
        TEST_CASE(every_choice_keeps_its_full_name_in_both_orientations),
        TEST_CASE(medium_landscape_shows_all_four_choices_and_the_explanation),
        TEST_CASE(the_portrait_prompt_moves_its_title_into_the_body),
        TEST_CASE(the_portrait_prompt_says_paused_in_the_body),
        TEST_CASE(the_paused_prompt_says_so_in_every_language_and_layout),
        TEST_CASE(the_prompt_counts_down_to_charging),
        TEST_CASE(the_prompt_lapses_after_fifteen_quiet_seconds),
        TEST_CASE(the_countdown_shows_in_the_header_without_the_charging_bolt),
        TEST_CASE(the_first_volume_press_pauses_auto_charge),
        TEST_CASE(other_keys_leave_the_countdown_running),
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
