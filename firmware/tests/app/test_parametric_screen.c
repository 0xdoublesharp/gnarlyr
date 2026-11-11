#include "check.h"
#include "default_theme.h"
#include "screens/parametric_screen.h"

static Settings settings;
static ParametricScreen editor;
static int committed;
static int previewed;
static int saved_slot;
static bool reject;
static Color pixels[480 * 222];

static bool commit(void *context, const ParametricEq *eq, int slot)
{
    (void)context;
    if (reject) {
        return false;
    }
    settings.parametric = *eq;
    settings.equalizer = EQUALIZER_PARAMETRIC;
    committed++;
    saved_slot = slot;
    return true;
}

static void preview(void *context, const SettingsEqualizerCurve *curve)
{
    (void)context;
    CHECK(curve->is_parametric || curve->band_count == 0);
    previewed++;
}

static void key(Key key)
{
    editor.screen.handle_key(&editor, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS}, 1000);
}

static void start(void)
{
    settings = settings_defaults();
    committed = 0;
    previewed = 0;
    saved_slot = -1;
    reject = false;
    parametric_screen_init(&editor, (ParametricScreenConfig){.settings = &settings,
                                                             .commit = commit,
                                                             .preview = preview,
                                                             .theme = &BOARD_THEME});
}

static void add_edit_cancel_keep_and_remove_work_with_buttons(void)
{
    start();
    key(KEY_PLAY);
    CHECK_EQUAL(1, settings.parametric.count);
    CHECK_EQUAL(1, committed);
    CHECK_EQUAL(0, editor.filter);
    key(KEY_VOLUME_UP);
    key(KEY_VOLUME_UP);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    CHECK_EQUAL(1, editor.working.filters[0].gain);
    CHECK_EQUAL(0, settings.parametric.filters[0].gain);
    CHECK(editor.previewing);
    key(KEY_BACK);
    CHECK_EQUAL(0, editor.working.filters[0].gain);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    CHECK_EQUAL(1, settings.parametric.filters[0].gain);
    CHECK_EQUAL(2, committed);
    key(KEY_VOLUME_UP);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    CHECK_EQUAL(0, settings.parametric.count);
    CHECK_EQUAL(-1, editor.filter);
}

static void failed_publication_keeps_the_saved_curve_and_slot_selection_works(void)
{
    start();
    reject = true;
    key(KEY_PLAY);
    CHECK_EQUAL(0, settings.parametric.count);
    CHECK_EQUAL(0, committed);
    reject = false;
    key(KEY_VOLUME_UP);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    CHECK_EQUAL(1, saved_slot);
}

static void every_native_layout_draws_the_editor_and_graph(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start();
            Theme theme = BOARD_THEME;
            default_theme_set_orientation(&theme, (Orientation)orientation);
            default_theme_set_text_size(&theme, (TextSize)size);
            editor.config.theme = &theme;
            editor.list.theme = &theme;
            key(KEY_PLAY);
            CHECK(editor.screen.draw != NULL);
            CHECK_EQUAL(1, editor.working.count);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            editor.screen.draw(&editor, &canvas);
            CHECK(editor.list.visible_rows > 0);
            CHECK(canvas_pixel_at(&canvas, 0, 0) != 0);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(add_edit_cancel_keep_and_remove_work_with_buttons),
        TEST_CASE(failed_publication_keeps_the_saved_curve_and_slot_selection_works),
        TEST_CASE(every_native_layout_draws_the_editor_and_graph),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
