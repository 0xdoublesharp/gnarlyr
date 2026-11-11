#include <stddef.h>
#include <string.h>

#include "check.h"
#include "ui/choice_screen.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    OPTION_COUNT = 5
};

static const char *const OPTIONS[OPTION_COUNT] = {"A", "B", "A", "B", "A"};
static int current_value;
static int chosen_value;
static ChoiceScreen choice;

static int current_option(const void *context)
{
    (void)context;
    return current_value;
}

static void choose_option(void *context, int index)
{
    (void)context;
    chosen_value = index;
}

static Screen follow_up;

static Screen *open_third(void *context, int index)
{
    (void)context;
    return index == 2 ? &follow_up : NULL;
}

static Screen *open_choice(int current)
{
    current_value = current;
    chosen_value = -1;
    ChoiceConfig config = {
        .title = "Pick",
        .options = OPTIONS,
        .option_count = OPTION_COUNT,
        .current = current_option,
        .choose = choose_option,
        .context = NULL,
        .theme = &TEST_THEME,
    };
    Screen *screen = choice_screen_init(&choice, config);
    screen->enter(screen->context, 0);
    return screen;
}

static ScreenAction press(Screen *screen, Key key, KeyGesture gesture)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = gesture}, 100);
}

static void options_given_as_string_ids_show_the_catalogue_text(void)
{
    static const StringId IDS[2] = {STR_OFF, STR_SETTINGS};
    ChoiceConfig config = {.title = "Pick",
                           .options = OPTIONS,
                           .option_ids = IDS,
                           .option_count = 2,
                           .current = current_option,
                           .choose = choose_option,
                           .theme = &TEST_THEME};
    choice_screen_init(&choice, config);
    CHECK(strcmp("Off", choice.model.label(choice.model.context, 0)) == 0);
    CHECK(strcmp("Settings", choice.model.label(choice.model.context, 1)) == 0);
}

static void options_given_as_text_are_shown_as_they_are(void)
{
    open_choice(0);
    CHECK(choice.model.label(choice.model.context, 1) == OPTIONS[1]);
}

static void the_choice_title_is_the_screen_title(void)
{
    Screen *screen = open_choice(0);
    CHECK(screen->title == choice.config.title);
}

static void the_current_option_starts_selected(void)
{
    open_choice(3);
    CHECK_EQUAL(3, choice.list.selected);
    CHECK_EQUAL(1, choice.list.top);
}

static void the_current_option_is_marked(void)
{
    open_choice(1);
    CHECK(choice.model.detail(choice.model.context, 1) != NULL);
    CHECK(choice.model.detail(choice.model.context, 0) == NULL);
}

static void selecting_chooses_the_option_and_leaves(void)
{
    Screen *screen = open_choice(0);
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    ScreenAction action = press(screen, KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(2, chosen_value);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
}

static void choosing_can_continue_into_another_screen(void)
{
    Screen *screen = open_choice(0);
    choice.config.open = open_third;
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    ScreenAction stay = press(screen, KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(SCREEN_ACTION_POP, stay.type);
    press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    ScreenAction next = press(screen, KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(2, chosen_value);
    CHECK_EQUAL(SCREEN_ACTION_REPLACE, next.type);
    CHECK(next.target == &follow_up);
}

static void back_leaves_without_choosing(void)
{
    Screen *screen = open_choice(2);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(screen, KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(-1, chosen_value);
}

static void the_list_is_drawn_under_the_header(void)
{
    Screen *screen = open_choice(0);
    Canvas canvas = ui_test_canvas();
    screen->update(screen->context, 100);
    screen->draw(screen->context, &canvas);
    Rect first_row = {TEST_SCREEN_WIDTH / 2, TEST_HEADER_HEIGHT, 1, TEST_ROW_HEIGHT};
    CHECK(ui_test_area_has_only(&canvas, first_row, TEST_THEME.colors.selection_background));
}

static const char *countdown_on_first(const void *context, int index)
{
    (void)context;
    return index == 0 ? "9 s" : NULL;
}

static void a_detail_callback_takes_the_place_of_the_check_mark(void)
{
    open_choice(1);
    choice.config.detail = countdown_on_first;
    CHECK(strcmp("9 s", choice.model.detail(choice.model.context, 0)) == 0);
    CHECK(choice.model.detail(choice.model.context, 1) == NULL);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_choice_title_is_the_screen_title),
        TEST_CASE(options_given_as_string_ids_show_the_catalogue_text),
        TEST_CASE(options_given_as_text_are_shown_as_they_are),
        TEST_CASE(the_current_option_starts_selected),
        TEST_CASE(the_current_option_is_marked),
        TEST_CASE(selecting_chooses_the_option_and_leaves),
        TEST_CASE(choosing_can_continue_into_another_screen),
        TEST_CASE(back_leaves_without_choosing),
        TEST_CASE(a_detail_callback_takes_the_place_of_the_check_mark),
        TEST_CASE(the_list_is_drawn_under_the_header),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
