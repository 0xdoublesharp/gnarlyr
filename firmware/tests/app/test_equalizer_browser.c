#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "equalizer_presets.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "hal/storage.h"
#include "screens/equalizer_browser.h"

static Settings settings;
static EqualizerPresets presets;
static EqualizerBrowser browser;
static SettingsEqualizerCurve audition;
static int previews;
static int commits;
static Color pixels[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];

static int current(const void *context)
{
    (void)context;
    return (int)settings.equalizer;
}

static void preview(void *context, const SettingsEqualizerCurve *curve)
{
    (void)context;
    audition = *curve;
    previews++;
}

static void choose(void *context, int option)
{
    (void)context;
    if (option < EQUALIZER_PRESET_COUNT) {
        settings.equalizer = (EqualizerPreset)option;
    } else if (equalizer_presets_load(&presets, option, settings.equalizer_gains)) {
        settings.equalizer = EQUALIZER_CUSTOM;
    }
    commits++;
}

static void reset(void)
{
    fake_hal_reset();
    fake_hal_clear_eq_files();
    i18n_detach();
    settings = settings_defaults();
    settings.equalizer = EQUALIZER_POP;
    previews = 0;
    commits = 0;
}

static void open_browser(void)
{
    equalizer_presets_scan(&presets);
    equalizer_browser_init(&browser,
                           (EqualizerBrowserConfig){.choice = {.title = i18n_text(STR_EQUALIZER),
                                                               .options = presets.options,
                                                               .option_count = presets.option_count,
                                                               .current = current,
                                                               .choose = choose,
                                                               .theme = &BOARD_THEME},
                                                    .settings = &settings,
                                                    .presets = &presets,
                                                    .preview = preview});
}

static void highlight(int option)
{
    list_view_select(&browser.choice.list, option, 1000);
    browser.choice.screen.update(&browser, 1000);
}

static void check_curve(SettingsEqualizerCurve expected)
{
    CHECK_EQUAL(expected.pre_gain, audition.pre_gain);
    CHECK_EQUAL(expected.band_count, audition.band_count);
    for (int band = 0; band < expected.band_count; band++) {
        CHECK_EQUAL(expected.band_hz[band], audition.band_hz[band]);
        CHECK_EQUAL(expected.gain_half_db[band], audition.gain_half_db[band]);
    }
}

static void highlighting_a_preset_auditions_without_committing_and_back_restores(void)
{
    reset();
    SettingsEqualizerCurve committed = settings_equalizer_curve(&settings);
    open_browser();
    highlight(EQUALIZER_BASS);
    CHECK(browser.valid);
    CHECK(browser.previewing);
    CHECK_EQUAL(EQUALIZER_POP, settings.equalizer);
    CHECK_EQUAL(0, commits);
    Settings bass = settings;
    bass.equalizer = EQUALIZER_BASS;
    check_curve(settings_equalizer_curve(&bass));
    ScreenAction action = browser.choice.screen.handle_key(
        &browser, (KeyEvent){.key = KEY_BACK, .gesture = KEY_GESTURE_PRESS}, 1000);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK(!browser.previewing);
    check_curve(committed);
}

static void response_and_audition_are_cached_between_selection_changes(void)
{
    reset();
    open_browser();
    highlight(EQUALIZER_JAZZ);
    int before = previews;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT});
    for (int frame = 0; frame < 3; frame++) {
        browser.choice.screen.update(&browser, (uint32_t)frame);
        browser.choice.screen.draw(&browser, &canvas);
    }
    CHECK_EQUAL(before, previews);
    CHECK_EQUAL(0, commits);
}

static void saved_and_file_curves_preview_without_mutating_custom_settings(void)
{
    reset();
    EqualizerSlots slots = {0};
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {6};
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_put(&slots, 2, gains);
    equalizer_slots_encode(&slots, bytes);
    CHECK(hal_internal_file_write(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes));
    fake_hal_add_eq_file("warm.txt", "GraphicEQ: 20 2; 20000 2\n");
    open_browser();
    highlight(EQUALIZER_PRESET_COUNT);
    CHECK(browser.valid);
    CHECK_EQUAL(1, audition.band_count);
    CHECK_EQUAL(6, audition.gain_half_db[0]);
    highlight(EQUALIZER_PRESET_COUNT + 1);
    CHECK(browser.valid);
    CHECK_EQUAL(SETTINGS_EQUALIZER_BANDS, audition.band_count);
    CHECK_EQUAL(4, audition.gain_half_db[0]);
    CHECK_EQUAL(EQUALIZER_POP, settings.equalizer);
    CHECK_EQUAL(0, settings.equalizer_gains[0]);
    CHECK_EQUAL(0, commits);
}

static void an_invalid_file_restores_the_committed_sound_and_cannot_be_applied(void)
{
    reset();
    fake_hal_add_eq_file("broken.txt", "no curve\n");
    SettingsEqualizerCurve committed = settings_equalizer_curve(&settings);
    open_browser();
    highlight(EQUALIZER_BASS);
    highlight(EQUALIZER_PRESET_COUNT);
    CHECK(!browser.valid);
    check_curve(committed);
    browser.choice.screen.handle_key(
        &browser, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS}, 1000);
    CHECK_EQUAL(0, commits);
    CHECK_EQUAL(EQUALIZER_POP, settings.equalizer);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(highlighting_a_preset_auditions_without_committing_and_back_restores),
        TEST_CASE(response_and_audition_are_cached_between_selection_changes),
        TEST_CASE(saved_and_file_curves_preview_without_mutating_custom_settings),
        TEST_CASE(an_invalid_file_restores_the_committed_sound_and_cannot_be_applied),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
