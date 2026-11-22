#include <stdbool.h>
#include <string.h>

#include "check.h"
#include "fonts/fonts.h"
#include "ui/confirm_screen.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    WIDE = 480,
    TALL = 222,
    NO_CHOICE = -1
};

static Color pixels[WIDE * TALL];
static int chosen;
static ConfirmScreen confirm;

static ScreenAction choose(void *context, int index)
{
    (void)context;
    chosen = index;
    return screen_action_pop();
}

static Theme real_theme(void)
{
    Theme theme = TEST_THEME;
    theme.fonts = (ThemeFonts){&FONT_INTER_SEMIBOLD_22, &FONT_INTER_REGULAR_22,
                               &FONT_INTER_REGULAR_16, &FONT_INTER_SEMIBOLD_22};
    theme.metrics.screen_width = WIDE;
    theme.metrics.screen_height = TALL;
    theme.metrics.header_height = 30;
    theme.metrics.row_height = 32;
    theme.metrics.padding = 12;
    theme.colors.accent = 0x07E0;
    return theme;
}

static Screen *open_confirm(const Theme *theme)
{
    chosen = NO_CHOICE;
    ConfirmConfig config = {.title = "Delete file?",
                            .subject = "A.flac",
                            .sections = {{"Clears:", "Totals"}, {NULL, "Gone for good."}},
                            .choices = {"Cancel", "Delete file"},
                            .choice_count = 2,
                            .first_danger = 1,
                            .choose = choose,
                            .theme = theme};
    Screen *screen = confirm_screen_init(&confirm, config);
    screen->enter(screen->context, 0);
    return screen;
}

static ScreenAction press(Screen *screen, Key key, KeyGesture gesture)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = gesture}, 100);
}

static void the_question_is_the_title(void)
{
    Theme theme = real_theme();
    CHECK(strcmp("Delete file?", open_confirm(&theme)->title) == 0);
}

static void the_safe_choice_is_first_and_focused(void)
{
    Theme theme = real_theme();
    open_confirm(&theme);
    CHECK_EQUAL(0, confirm.list.selected);
    CHECK_EQUAL(LIST_ROW_PLAIN, confirm.model.role(confirm.model.context, 0));
}

static void destructive_choices_take_the_danger_role(void)
{
    Theme theme = real_theme();
    open_confirm(&theme);
    CHECK_EQUAL(LIST_ROW_DANGER, confirm.model.role(confirm.model.context, 1));
}

static void back_cancels(void)
{
    Theme theme = real_theme();
    Screen *screen = open_confirm(&theme);
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    ScreenAction action = press(screen, KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(CONFIRM_CANCEL, chosen);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
}

static void hold_back_goes_home_without_choosing(void)
{
    Theme theme = real_theme();
    Screen *screen = open_confirm(&theme);
    ScreenAction action = press(screen, KEY_BACK, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(NO_CHOICE, chosen);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, action.type);
}

static void a_fresh_play_on_the_destructive_row_commits(void)
{
    Theme theme = real_theme();
    Screen *screen = open_confirm(&theme);
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    press(screen, KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(1, chosen);
}

static void a_held_or_repeated_play_chooses_nothing(void)
{
    Theme theme = real_theme();
    Screen *screen = open_confirm(&theme);
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    press(screen, KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    press(screen, KEY_PLAY, KEY_GESTURE_REPEAT);
    CHECK_EQUAL(NO_CHOICE, chosen);
}

static void the_footer_band_closes_the_screen(void)
{
    Theme theme = real_theme();
    Screen *screen = open_confirm(&theme);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, WIDE, TALL});
    canvas_clear(&canvas, 0x0821);
    screen->draw(screen->context, &canvas);
    CHECK(ui_test_area_has_only(&canvas, (Rect){0, TALL - 24, WIDE, 1}, theme.colors.divider));
    CHECK(ui_test_area_has_color(&canvas, (Rect){WIDE / 2, TALL - 23, WIDE / 2, 22},
                                 theme.colors.header_text));
}

static void choices_sit_above_the_footer_under_the_consequences(void)
{
    Theme theme = real_theme();
    Screen *screen = open_confirm(&theme);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, WIDE, TALL});
    canvas_clear(&canvas, 0x0821);
    screen->draw(screen->context, &canvas);
    Rect rows = {0, TALL - 24 - 64, WIDE, 64};
    Rect danger_row = {0, TALL - 24 - 32, WIDE, 32};
    CHECK(ui_test_area_has_color(&canvas, (Rect){0, rows.y, WIDE, 32},
                                 theme.colors.selection_background));
    CHECK(ui_test_area_has_color(&canvas, danger_row, theme.colors.danger));
    CHECK(ui_test_area_has_color(&canvas, (Rect){0, 30, WIDE, rows.y - 30}, theme.colors.accent));
    CHECK(ui_test_area_has_color(&canvas, (Rect){0, 30, WIDE, rows.y - 30},
                                 theme.colors.text_secondary));
}

static void a_portrait_dialog_keeps_its_choices_whole(void)
{
    Theme theme = real_theme();
    theme.metrics.screen_width = TALL;
    theme.metrics.screen_height = WIDE;
    Screen *screen = open_confirm(&theme);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, TALL, WIDE});
    canvas_clear(&canvas, 0x0821);
    screen->draw(screen->context, &canvas);
    CHECK_EQUAL(2, confirm.list.visible_rows);
    CHECK(
        ui_test_area_has_color(&canvas, (Rect){0, WIDE - 48 - 32, TALL, 32}, theme.colors.danger));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_question_is_the_title),
        TEST_CASE(the_safe_choice_is_first_and_focused),
        TEST_CASE(destructive_choices_take_the_danger_role),
        TEST_CASE(back_cancels),
        TEST_CASE(hold_back_goes_home_without_choosing),
        TEST_CASE(a_fresh_play_on_the_destructive_row_commits),
        TEST_CASE(a_held_or_repeated_play_chooses_nothing),
        TEST_CASE(the_footer_band_closes_the_screen),
        TEST_CASE(choices_sit_above_the_footer_under_the_consequences),
        TEST_CASE(a_portrait_dialog_keeps_its_choices_whole),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
