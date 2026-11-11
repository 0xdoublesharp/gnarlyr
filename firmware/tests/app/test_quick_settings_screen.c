#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "gfx/text.h"
#include "screens/quick_settings_screen.h"
#include "settings_actions.h"
#include "text_spy.h"

static QuickSettingsScreen panel;
static SettingsStore settings;
static HeadphoneStore headphones;
static Player player;
static FixtureLibrary fixture;
static Theme theme;
static PowerPolicy power;
static Toast toast;
static Screen lyrics;
static Screen up_next;
static Color pixels[480 * 480];
static bool speed_available;
static int speed_cycles;

static void start(void)
{
    fake_hal_reset();
    i18n_detach();
    CHECK(fixture_library_open(&fixture));
    player_init(&player, &fixture.library);
    settings_store_load(&settings);
    CHECK_EQUAL(HAL_RESERVE_CREATED,
                hal_internal_file_reserve(HEADPHONE_STORE_PATH, HEADPHONE_FILE_BYTES));
    headphone_store_load(&headphones);
    power_policy_init(&power, 30000, 100);
    toast = (Toast){0};
    theme = BOARD_THEME;
    quick_settings_screen_init(&panel, (QuickSettingsConfig){.settings = {.store = &settings,
                                                                          .headphones = &headphones,
                                                                          .player = &player,
                                                                          .power = &power,
                                                                          .toast = &toast},
                                                             .theme = &theme,
                                                             .lyrics = &lyrics,
                                                             .up_next = &up_next});
    panel.screen.enter(&panel, 100);
}

static ScreenAction press(Key key)
{
    return panel.screen.handle_key(&panel, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                                   100);
}

static void choose(int index)
{
    list_view_select(&panel.list, index, 100);
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(KEY_PLAY).type);
}

static void navigation_wraps_and_back_closes_without_changing_a_value(void)
{
    start();
    CHECK_EQUAL(9, panel.model.count(&panel));
    press(KEY_VOLUME_DOWN);
    CHECK_EQUAL(8, panel.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, press(KEY_PLAY).type);
    CHECK(press(KEY_PLAY).target == &up_next);
    press(KEY_VOLUME_UP);
    CHECK_EQUAL(0, panel.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
    CHECK_EQUAL(SHUFFLE_OFF, player.shuffle);
    CHECK_EQUAL(0, fake_hal_write_count());
    list_view_select(&panel.list, 7, 100);
    CHECK(press(KEY_PLAY).target == &lyrics);
    panel.screen.enter(&panel, 200);
    CHECK_EQUAL(7, panel.list.selected);
}

static void every_eq_preset_and_used_slot_cycles_and_persists(void)
{
    start();
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {2, -2, 4, -4, 6, -6, 8, -8, 10, -10};
    CHECK(equalizer_presets_save_slot(panel.config.settings.presets, 5, gains));
    panel.screen.enter(&panel, 100);
    for (int index = 1; index < EQUALIZER_PRESET_COUNT; index++) {
        choose(2);
        CHECK_EQUAL(index, settings.settings.equalizer);
        CHECK(strcmp(panel.model.detail(&panel, 2), i18n_text(EQUALIZER_PRESET_NAMES[index])) == 0);
    }
    choose(2);
    CHECK_EQUAL(EQUALIZER_CUSTOM, settings.settings.equalizer);
    CHECK(memcmp(gains, settings.settings.equalizer_gains, sizeof gains) == 0);
    CHECK(strcmp(panel.model.detail(&panel, 2), equalizer_presets_slot_label(5)) == 0);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK(memcmp(gains, restored.settings.equalizer_gains, sizeof gains) == 0);
    settings_store_apply_hardware(&restored.settings);
    CHECK_EQUAL(SETTINGS_EQUALIZER_BANDS, fake_hal_audio_equalizer().band_count);
    choose(2);
    CHECK_EQUAL(EQUALIZER_OFF, settings.settings.equalizer);
}

static void headphone_profiles_cycle_through_off_and_survive_reload(void)
{
    start();
    for (int index = 0; index <= HEADPHONE_PROFILE_LIMIT; index++) {
        choose(3);
        HeadphoneStore restored;
        headphone_store_load(&restored);
        CHECK_EQUAL(index, restored.profiles.current);
        const char *text = index == HEADPHONE_PROFILE_NONE ? i18n_text(STR_OFF)
                                                           : restored.profiles.slots[index].name;
        CHECK(strcmp(panel.model.detail(&panel, 3), text) == 0);
    }
}

static void sleep_cycles_the_same_session_timer_and_does_not_persist(void)
{
    start();
    for (int option = 1; option < SLEEP_TIMER_OPTION_COUNT; option++) {
        choose(4);
        CHECK(power.sleep_timer_active);
        CHECK_EQUAL(settings_sleep_option_minutes(option) * 60000, power.sleep_duration_ms);
        CHECK_EQUAL(option, settings_action_current_sleep(&panel.config.settings));
    }
    SettingsContext other = panel.config.settings;
    settings_action_choose_sleep(&other, 1);
    CHECK_EQUAL(1, settings_action_current_sleep(&panel.config.settings));
    choose(4);
    CHECK_EQUAL(2, settings_action_current_sleep(&other));
    settings_action_choose_sleep(&other, 0);
    CHECK(!power.sleep_timer_active);
    CHECK_EQUAL(0, fake_hal_write_count());
}

static bool speed_is_available(const void *context)
{
    return *(const bool *)context;
}

static const char *speed_value(const void *context)
{
    (void)context;
    return "1.25x";
}

static void cycle_speed(void *context)
{
    (void)context;
    speed_cycles++;
}

static void speed_is_hidden_until_a_complete_available_hook_is_supplied(void)
{
    start();
    speed_available = false;
    speed_cycles = 0;
    panel.config.speed =
        (QuickSettingsSpeed){&speed_available, speed_is_available, speed_value, cycle_speed};
    CHECK_EQUAL(9, panel.model.count(&panel));
    speed_available = true;
    CHECK_EQUAL(10, panel.model.count(&panel));
    CHECK(strcmp(panel.model.label(&panel, 4), i18n_text(STR_PLAYBACK_SPEED)) == 0);
    CHECK(strcmp(panel.model.detail(&panel, 4), "1.25x") == 0);
    choose(4);
    CHECK_EQUAL(1, speed_cycles);
    CHECK(!power.sleep_timer_active);
    panel.config.speed.cycle = NULL;
    CHECK_EQUAL(9, panel.model.count(&panel));
}

static void losing_speed_support_keeps_the_selection_inside_the_panel(void)
{
    start();
    speed_available = true;
    panel.config.speed =
        (QuickSettingsSpeed){&speed_available, speed_is_available, speed_value, cycle_speed};
    list_view_select(&panel.list, 9, 100);
    speed_available = false;
    panel.screen.update(&panel, 200);
    CHECK_EQUAL(8, panel.list.selected);
    CHECK(press(KEY_PLAY).target == &up_next);
}

static void replay_gain_and_transitions_update_the_player_and_keep_other_settings(void)
{
    start();
    settings.settings.prevent_clipping = false;
    settings.settings.transition_tenths = 27;
    for (int mode = 1; mode <= REPLAY_GAIN_MODE_COUNT; mode++) {
        choose(6);
        CHECK_EQUAL(mode % REPLAY_GAIN_MODE_COUNT, player.replay_gain_mode);
        CHECK(!player.prevent_clipping);
        SettingsStore restored;
        settings_store_load(&restored);
        CHECK_EQUAL(player.replay_gain_mode, restored.settings.replay_gain_mode);
        CHECK(!restored.settings.prevent_clipping);
    }
    choose(5);
    CHECK(player.fade_transitions);
    CHECK_EQUAL(27, player.transition_tenths);
    choose(5);
    CHECK(!player.fade_transitions);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(TRACK_TRANSITION_OFF, restored.settings.track_transition);
    CHECK_EQUAL(27, restored.settings.transition_tenths);
}

static void failed_saves_are_reported_and_headphone_selection_stays_committed(void)
{
    start();
    fake_hal_fail_internal_writes(SETTINGS_STORE_PATH);
    choose(0);
    CHECK(strcmp(toast.text, i18n_text(STR_SETTINGS_NOT_SAVED)) == 0);
    start();
    fake_hal_fail_internal_writes(HEADPHONE_STORE_PATH);
    choose(3);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, headphones.profiles.current);
    CHECK(strcmp(toast.text, i18n_text(STR_SETTINGS_NOT_SAVED)) == 0);
}

static void all_rows_fit_every_theme_orientation_and_text_size(void)
{
    start();
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                theme = BOARD_THEME;
                default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
                default_theme_set_text_size(&theme, (TextSize)size);
                default_theme_set_orientation(&theme, (Orientation)orientation);
                panel.screen.enter(&panel, 100);
                CHECK(panel.list.visible_rows > 0);
                CHECK(text_width(theme.fonts.small, i18n_text(STR_QUICK_SETTINGS_HINT)) <=
                      theme.metrics.screen_width - 2 * theme.metrics.padding);
                for (int row = 0; row < panel.model.count(&panel); row++) {
                    list_view_select(&panel.list, row, 100);
                    Canvas canvas = canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width,
                                                               theme.metrics.screen_height});
                    text_spy_reset();
                    panel.screen.draw(&panel, &canvas);
                    CHECK(panel.list.top <= row);
                    CHECK(panel.list.top + panel.list.visible_rows > row);
                }
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(navigation_wraps_and_back_closes_without_changing_a_value),
        TEST_CASE(every_eq_preset_and_used_slot_cycles_and_persists),
        TEST_CASE(headphone_profiles_cycle_through_off_and_survive_reload),
        TEST_CASE(sleep_cycles_the_same_session_timer_and_does_not_persist),
        TEST_CASE(speed_is_hidden_until_a_complete_available_hook_is_supplied),
        TEST_CASE(losing_speed_support_keeps_the_selection_inside_the_panel),
        TEST_CASE(replay_gain_and_transitions_update_the_player_and_keep_other_settings),
        TEST_CASE(failed_saves_are_reported_and_headphone_selection_stays_committed),
        TEST_CASE(all_rows_fit_every_theme_orientation_and_text_size),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
