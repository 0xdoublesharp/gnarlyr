#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "screens/equalizer_plot.h"
#include "screens/equalizer_screen.h"
#include "ui/label.h"

static Settings settings;
static EqualizerScreen equalizer;
static int changed;
static int finished;
static int previewed;
static SettingsEqualizerCurve audition;

static void apply(void *context, bool done)
{
    (void)context;
    changed++;
    finished += done ? 1 : 0;
}

static void preview(void *context, const SettingsEqualizerCurve *curve)
{
    (void)context;
    previewed++;
    audition = *curve;
}

static bool unused(const void *context, int slot)
{
    (void)context;
    (void)slot;
    return false;
}

static void save_slot(void *context, int slot)
{
    (void)context;
    (void)slot;
}

static void key(Key key)
{
    equalizer.screen.handle_key(&equalizer, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                                1000);
}

static void start_with_gain(int gain)
{
    settings = settings_defaults();
    settings.equalizer = EQUALIZER_CUSTOM;
    settings.equalizer_gains[0] = (int8_t)gain;
    changed = 0;
    finished = 0;
    previewed = 0;
    equalizer_screen_init(&equalizer, (EqualizerScreenConfig){.settings = &settings,
                                                              .changed = apply,
                                                              .preview = preview,
                                                              .slot_used = unused,
                                                              .save_slot = save_slot,
                                                              .theme = &BOARD_THEME});
    equalizer.screen.enter(&equalizer, 0);
}

static void browsing_bands_does_not_change_the_sound(void)
{
    start_with_gain(0);
    key(KEY_VOLUME_UP);
    CHECK_EQUAL(1, equalizer.list.selected);
    CHECK_EQUAL(0, changed);
    CHECK(!equalizer.adjusting);
}

static void adjustment_keeps_half_decibel_precision_and_play_confirms(void)
{
    start_with_gain(0);
    key(KEY_PLAY);
    CHECK(equalizer.adjusting);
    key(KEY_VOLUME_UP);
    CHECK_EQUAL(1, equalizer.working.equalizer_gains[0]);
    CHECK_EQUAL(0, settings.equalizer_gains[0]);
    CHECK_EQUAL(1, previewed);
    CHECK_EQUAL(0, changed);
    key(KEY_PLAY);
    CHECK(!equalizer.adjusting);
    CHECK_EQUAL(1, settings.equalizer_gains[0]);
    CHECK_EQUAL(1, finished);
}

static void back_cancels_the_current_band_adjustment(void)
{
    start_with_gain(-3);
    key(KEY_PLAY);
    key(KEY_VOLUME_UP);
    CHECK_EQUAL(-2, equalizer.working.equalizer_gains[0]);
    CHECK_EQUAL(-3, settings.equalizer_gains[0]);
    key(KEY_BACK);
    CHECK(!equalizer.adjusting);
    CHECK_EQUAL(-3, settings.equalizer_gains[0]);
    CHECK_EQUAL(-3, equalizer.working.equalizer_gains[0]);
    CHECK_EQUAL(-3, audition.gain_half_db[0]);
    CHECK_EQUAL(0, finished);
}

static void check_area(Rect area, const Theme *theme)
{
    CHECK(area.x >= 0);
    CHECK(area.y >= 0);
    CHECK(area.width > 0);
    CHECK(area.height > 0);
    CHECK(area.x + area.width <= theme->metrics.screen_width);
    CHECK(area.y + area.height <= theme->metrics.screen_height);
}

static void curves_controls_and_hints_fit_all_six_native_layouts(void)
{
    static const StringId hints[] = {STR_EQ_BROWSE_HINT, STR_EQ_ADJUST_HINT, STR_EQ_SLOT_HINT,
                                     STR_EQ_PRESET_HINT};
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            for (int index = 0; index < 4; index++) {
                const char *hint = i18n_text(hints[index]);
                EqualizerPlotLayout layout = equalizer_plot_layout(&theme, hint, index == 3);
                check_area(layout.primary, &theme);
                check_area(layout.plot, &theme);
                check_area(layout.preamp, &theme);
                check_area(layout.reference, &theme);
                check_area(layout.hint, &theme);
                CHECK(layout.plot.height >= 2 * font_line_height(theme.fonts.small));
                CHECK(layout.plot.y + layout.plot.height <= layout.reference.y);
                CHECK(layout.hint.y >= layout.reference.y + layout.reference.height);
                CHECK(layout.hint.height >=
                      label_wrap_count(theme.fonts.small, hint, layout.hint.width) *
                          font_line_height(theme.fonts.small));
                if (index == 3) {
                    check_area(layout.list, &theme);
                    CHECK(layout.list.height >= theme.metrics.row_height);
                } else {
                    check_area(layout.secondary, &theme);
                }
            }
        }
    }
}

int main(void)
{
    i18n_detach();
    static const TestCase cases[] = {
        TEST_CASE(curves_controls_and_hints_fit_all_six_native_layouts),
        TEST_CASE(browsing_bands_does_not_change_the_sound),
        TEST_CASE(adjustment_keeps_half_decibel_precision_and_play_confirms),
        TEST_CASE(back_cancels_the_current_band_adjustment),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
