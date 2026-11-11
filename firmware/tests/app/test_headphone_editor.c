#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "headphone_editor.h"
#include "screen_snapshot.h"
#include "text_spy.h"

static HeadphoneEditor editor;
static HeadphoneStore store;
static SettingsStore settings;
static Player player;
static FixtureLibrary fixture;
static Theme theme;
static Toast toast;
static Color pixels[480 * 480];

static void start(TextSize size, Orientation orientation)
{
    fake_hal_reset();
    i18n_detach();
    CHECK(fixture_library_open(&fixture));
    player_init(&player, &fixture.library);
    settings_store_load(&settings);
    CHECK_EQUAL(HAL_RESERVE_CREATED,
                hal_internal_file_reserve(HEADPHONE_STORE_PATH, HEADPHONE_FILE_BYTES));
    headphone_store_load(&store);
    theme = BOARD_THEME;
    default_theme_set_text_size(&theme, size);
    default_theme_set_orientation(&theme, orientation);
    toast = (Toast){0};
    headphone_editor_init(&editor,
                          (HeadphoneEditorConfig){&store, &player, &settings, &theme, &toast});
}

static ScreenAction press(Key key)
{
    return editor.screen.handle_key(&editor, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                                    100);
}

static void select_row(int row)
{
    list_view_select(&editor.list, row, 0);
    press(KEY_PLAY);
}

static void profiles_can_be_selected_and_edited_without_leaving_the_screen(void)
{
    start(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    CHECK_EQUAL(5, editor.model.count(&editor));
    select_row(0);
    CHECK_EQUAL(5, editor.model.count(&editor));
    select_row(3);
    CHECK_EQUAL(2, store.profiles.current);
    select_row(3);
    CHECK_EQUAL(5, editor.model.count(&editor));
    select_row(2);
    CHECK_EQUAL(25, editor.model.count(&editor));
    select_row(24);
    CHECK_EQUAL(24, store.profiles.slots[2].trim_half_db);
    select_row(3);
    CHECK_EQUAL(201, editor.model.count(&editor));
    select_row(125);
    CHECK_EQUAL(25, store.profiles.slots[2].balance);
    select_row(4);
    CHECK_EQUAL(61, editor.model.count(&editor));
    select_row(18);
    CHECK_EQUAL(18, store.profiles.slots[2].volume_ceiling);
    select_row(1);
    select_row(0);
    CHECK(strcmp(i18n_text(STR_HEADPHONE_IN_EAR), store.profiles.slots[2].name) == 0);
    press(KEY_BACK);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
}

static void back_cancels_an_edit_and_the_editor_reopens_the_saved_choice(void)
{
    start(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    select_row(1);
    select_row(2);
    list_view_select(&editor.list, 24, 0);
    press(KEY_BACK);
    CHECK_EQUAL(0, store.profiles.slots[0].trim_half_db);
    select_row(2);
    CHECK_EQUAL(0, editor.list.selected);
    press(KEY_BACK);
    press(KEY_BACK);
    select_row(0);
    CHECK_EQUAL(0, editor.list.selected);
}

static void every_orientation_and_text_size_draws_the_editor_and_choice_bounds(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start((TextSize)size, (Orientation)orientation);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            text_spy_reset();
            canvas_clear(&canvas, theme.colors.background);
            editor.screen.draw(&editor, &canvas);
            CHECK(text_spy_count() > 0);
            screen_snapshot_write(&canvas, "headphones", orientation * 3 + size);
            select_row(1);
            canvas_clear(&canvas, theme.colors.background);
            editor.screen.draw(&editor, &canvas);
            screen_snapshot_write(&canvas, "headphone-edit", orientation * 3 + size);
            for (int field = 1; field <= 4; field++) {
                select_row(field);
                int last = editor.model.count(&editor) - 1;
                list_view_select(&editor.list, last, 0);
                canvas_clear(&canvas, theme.colors.background);
                editor.screen.draw(&editor, &canvas);
                CHECK(last >= editor.list.top);
                CHECK(last < editor.list.top + editor.list.visible_rows);
                screen_snapshot_write(&canvas, "headphone-choice",
                                      (orientation * 3 + size) * 4 + field);
                press(KEY_BACK);
            }
        }
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(profiles_can_be_selected_and_edited_without_leaving_the_screen),
        TEST_CASE(back_cancels_an_edit_and_the_editor_reopens_the_saved_choice),
        TEST_CASE(every_orientation_and_text_size_draws_the_editor_and_choice_bounds),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
