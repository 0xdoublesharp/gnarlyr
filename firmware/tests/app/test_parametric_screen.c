#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "screens/equalizer_plot.h"
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

static void adjusting_keeps_the_full_preamp_caption_visible_in_every_layout(void)
{
    static Color expected_pixels[480 * 222];
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start();
            Theme theme = BOARD_THEME;
            default_theme_set_orientation(&theme, (Orientation)orientation);
            default_theme_set_text_size(&theme, (TextSize)size);
            settings.parametric =
                (ParametricEq){.count = 2,
                               .preamp = -62,
                               .filters = {{105, 70, -23, PARAMETRIC_LOW_SHELF, true},
                                           {1000, 140, 62, PARAMETRIC_PEAK, true}}};
            ParametricScreenConfig config = editor.config;
            config.theme = &theme;
            parametric_screen_init(&editor, config);
            key(KEY_PLAY);
            key(KEY_VOLUME_UP);
            key(KEY_VOLUME_UP);
            key(KEY_VOLUME_UP);
            key(KEY_PLAY);
            EqualizerPlotLayout layout =
                equalizer_plot_layout(&theme, i18n_text(STR_PEQ_EDIT_HINT), true);
            Canvas actual = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            for (int y = 0; y < theme.metrics.screen_height; y += 23) {
                int height =
                    theme.metrics.screen_height - y < 23 ? theme.metrics.screen_height - y : 23;
                Canvas strip = canvas_make(pixels + y * theme.metrics.screen_width,
                                           (Rect){0, y, theme.metrics.screen_width, height});
                editor.screen.draw(&editor, &strip);
            }
            Canvas expected = canvas_make(expected_pixels, actual.bounds);
            canvas_clear(&expected, theme.colors.background);
            equalizer_plot_caption(&expected, &theme, layout.preamp, &editor.response);
            for (int y = layout.preamp.y; y < layout.preamp.y + layout.preamp.height; y++) {
                for (int x = layout.preamp.x; x < layout.preamp.x + layout.preamp.width; x++) {
                    CHECK_EQUAL(canvas_pixel_at(&expected, x, y), canvas_pixel_at(&actual, x, y));
                }
            }
        }
    }
}

static bool shape_commit(void *context, const ParametricEq *eq, const SoundShaping *shape, int slot)
{
    CHECK(shape->active);
    return commit(context, eq, slot);
}

static void shaping_previews_cancels_saves_resets_and_refuses_an_overfull_stack(void)
{
    start();
    ParametricBank bank = {0};
    bank.used = 1;
    bank.slots[0].count = 10;
    for (int index = 0; index < 10; index++) {
        bank.slots[0].filters[index] = (ParametricFilter){1000, 70, -10, PARAMETRIC_PEAK, true};
    }
    editor.config.bank = &bank;
    editor.config.shape_commit = shape_commit;
    editor.shaping_requested = true;
    editor.screen.enter(&editor, 0);
    CHECK(editor.shaping_mode);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    CHECK_EQUAL(1, editor.shaping.positions[0]);
    CHECK_EQUAL(0, committed);
    CHECK_EQUAL(1, previewed);
    key(KEY_BACK);
    CHECK_EQUAL(0, editor.shaping.positions[0]);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    CHECK_EQUAL(1, committed);
    CHECK_EQUAL(1, settings.parametric.count);
    for (int index = 0; index < 7; index++) {
        key(KEY_VOLUME_UP);
    }
    key(KEY_PLAY);
    CHECK_EQUAL(0, settings.parametric.count);
    CHECK_EQUAL(0, editor.shaping.positions[0]);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    CHECK_EQUAL(1, saved_slot);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    key(KEY_PLAY);
    CHECK_EQUAL(10, settings.parametric.count);
    CHECK_EQUAL(10, editor.shaping.base_count);
    for (int index = 0; index < 9; index++) {
        key(KEY_VOLUME_DOWN);
    }
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    CHECK_EQUAL(0, editor.shaping.positions[0]);
    CHECK_EQUAL(STR_SHAPE_LIMIT, editor.notice);
    CHECK_EQUAL(10, settings.parametric.count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(shaping_previews_cancels_saves_resets_and_refuses_an_overfull_stack),
        TEST_CASE(add_edit_cancel_keep_and_remove_work_with_buttons),
        TEST_CASE(failed_publication_keeps_the_saved_curve_and_slot_selection_works),
        TEST_CASE(every_native_layout_draws_the_editor_and_graph),
        TEST_CASE(adjusting_keeps_the_full_preamp_caption_visible_in_every_layout),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
