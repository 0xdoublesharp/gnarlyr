#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "equalizer_ui.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "gfx/text.h"
#include "screen_snapshot.h"
#include "screens/quick_settings_screen.h"
#include "settings_actions.h"
#include "text_spy.h"
#include "ui/footer_hints.h"

enum {
    SHUFFLE_ROW = 0,
    REPEAT_ROW = 1,
    EQUALIZER_ROW = 2,
    HEADPHONES_ROW = 3,
    SPEED_ROW = 4,
    SLEEP_ROW = 5,
    TRANSITIONS_ROW = 6,
    REPLAY_GAIN_ROW = 7,
    AB_ROW = 8,
    STYLE_ROW = 9,
    LYRICS_ROW = 10,
    UP_NEXT_ROW = 11,
    ROWS = 12
};

static const StringId STYLE_VALUES[] = {STR_NOW_PLAYING_COVER, STR_NOW_PLAYING_CASSETTE,
                                        STR_NOW_PLAYING_VU, STR_NOW_PLAYING_SPECTRUM};
static const int SUBMENU_ROWS[] = {EQUALIZER_ROW, HEADPHONES_ROW,  SPEED_ROW,
                                   SLEEP_ROW,     REPLAY_GAIN_ROW, STYLE_ROW};
static const char *const SUBMENU_NAMES[] = {"equalizer", "headphones",  "speed",
                                            "sleep",     "replay-gain", "style"};

static QuickSettingsScreen panel;
static EqualizerUi equalizer;
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
    CHECK_EQUAL(HAL_RESERVE_CREATED, hal_internal_file_reserve(PARAMETRIC_STORE_PATH, 4096));
    power_policy_init(&power, 30000, 100);
    toast = (Toast){0};
    theme = BOARD_THEME;
    equalizer_ui_init(&equalizer, &settings, &player, &toast, &theme);
    quick_settings_screen_init(
        &panel, (QuickSettingsConfig){.settings = {.store = &settings,
                                                   .headphones = &headphones,
                                                   .player = &player,
                                                   .power = &power,
                                                   .equalizer = &equalizer.browser.choice.screen,
                                                   .presets = equalizer.presets,
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

static ScreenAction press_values(Key key)
{
    return panel.values.screen.handle_key(
        &panel.values, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS}, 100);
}

static void choose(int index)
{
    list_view_select(&panel.list, index, 100);
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(KEY_PLAY).type);
}

static void open_values(int row)
{
    list_view_select(&panel.list, row, 100);
    ScreenAction action = press(KEY_PLAY);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &panel.values.screen);
    panel.values.screen.enter(&panel.values, 100);
}

static void pick(int row, int value)
{
    open_values(row);
    list_view_select(&panel.values.list, value, 100);
    CHECK_EQUAL(SCREEN_ACTION_POP, press_values(KEY_PLAY).type);
    panel.screen.enter(&panel, 200);
    CHECK_EQUAL(row, panel.list.selected);
}

static bool row_detail_is(int row, StringId expected)
{
    return strcmp(i18n_text(expected), panel.model.detail(&panel, row)) == 0;
}

static bool value_label_is(int value, const char *expected)
{
    return strcmp(expected, panel.values.model.label(&panel.values, value)) == 0;
}

static void navigation_wraps_and_back_closes_without_changing_a_value(void)
{
    start();
    CHECK_EQUAL(ROWS, panel.model.count(&panel));
    press(KEY_VOLUME_DOWN);
    CHECK_EQUAL(UP_NEXT_ROW, panel.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, press(KEY_PLAY).type);
    CHECK(press(KEY_PLAY).target == &up_next);
    press(KEY_VOLUME_UP);
    CHECK_EQUAL(0, panel.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
    CHECK_EQUAL(SHUFFLE_OFF, player.shuffle);
    CHECK_EQUAL(0, fake_hal_write_count());
    list_view_select(&panel.list, LYRICS_ROW, 100);
    CHECK(press(KEY_PLAY).target == &lyrics);
    panel.screen.enter(&panel, 200);
    CHECK_EQUAL(LYRICS_ROW, panel.list.selected);
}

static void rows_with_four_or_more_values_open_a_submenu_and_the_rest_act_in_place(void)
{
    start();
    player.loaded = true;
    for (int row = 0; row < ROWS; row++) {
        bool submenu = false;
        for (size_t index = 0; index < sizeof SUBMENU_ROWS / sizeof SUBMENU_ROWS[0]; index++) {
            submenu = submenu || SUBMENU_ROWS[index] == row;
        }
        bool push = row == LYRICS_ROW || row == UP_NEXT_ROW;
        list_view_select(&panel.list, row, 100);
        CHECK_EQUAL(submenu || push, panel.model.opens(&panel, row));
        ScreenAction action = press(KEY_PLAY);
        CHECK_EQUAL(submenu || push ? SCREEN_ACTION_PUSH : SCREEN_ACTION_NONE, action.type);
        if (submenu) {
            CHECK(action.target == &panel.values.screen);
            CHECK(panel.values.config.option_count >= 4);
        }
    }
}

static void cycle_rows_wrap_to_their_first_value(void)
{
    start();
    for (int index = 1; index <= SHUFFLE_MODE_COUNT; index++) {
        choose(SHUFFLE_ROW);
        CHECK_EQUAL(index % SHUFFLE_MODE_COUNT,
                    settings_action_current_shuffle(&panel.config.settings));
    }
    for (int index = 1; index <= REPEAT_MODE_COUNT; index++) {
        choose(REPEAT_ROW);
        CHECK_EQUAL(index % REPEAT_MODE_COUNT,
                    settings_action_current_repeat(&panel.config.settings));
    }
    choose(TRANSITIONS_ROW);
    CHECK(row_detail_is(TRANSITIONS_ROW, STR_TRANSITION_FADE));
    choose(TRANSITIONS_ROW);
    CHECK(row_detail_is(TRANSITIONS_ROW, STR_OFF));
}

static void a_submenu_marks_and_highlights_the_current_value_on_entry(void)
{
    start();
    settings_action_choose_now_playing(&panel.config.settings, NOW_PLAYING_VU);
    open_values(STYLE_ROW);
    CHECK(strcmp(i18n_text(STR_NOW_PLAYING_STYLE), panel.values.screen.title) == 0);
    CHECK_EQUAL(4, panel.values.model.count(&panel.values));
    CHECK_EQUAL(NOW_PLAYING_VU, panel.values.list.selected);
    for (int value = 0; value < 4; value++) {
        CHECK(value_label_is(value, i18n_text(STYLE_VALUES[value])));
        CHECK_EQUAL(value == NOW_PLAYING_VU,
                    panel.values.model.detail(&panel.values, value) != NULL);
    }
}

static Settings snapshot_settings(void)
{
    return settings.settings;
}

static void cancelling_any_submenu_leaves_every_value_unchanged(void)
{
    start();
    for (size_t index = 0; index < sizeof SUBMENU_ROWS / sizeof SUBMENU_ROWS[0]; index++) {
        Settings before = snapshot_settings();
        int writes = fake_hal_write_count();
        open_values(SUBMENU_ROWS[index]);
        press_values(KEY_VOLUME_UP);
        press_values(KEY_VOLUME_UP);
        CHECK_EQUAL(SCREEN_ACTION_POP, press_values(KEY_BACK).type);
        Settings after = snapshot_settings();
        CHECK(memcmp(&before, &after, sizeof before) == 0);
        CHECK_EQUAL(writes, fake_hal_write_count());
        CHECK_EQUAL(HEADPHONE_PROFILE_NONE, headphones.profiles.current);
        CHECK(!power.sleep_timer_active);
    }
}

static void every_eq_preset_and_used_slot_can_be_chosen_and_persists(void)
{
    start();
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {2, -2, 4, -4, 6, -6, 8, -8, 10, -10};
    CHECK(equalizer_presets_save_slot(panel.config.settings.presets, 5, gains));
    panel.screen.enter(&panel, 100);
    for (int index = 1; index < EQUALIZER_PRESET_COUNT; index++) {
        pick(EQUALIZER_ROW, index);
        CHECK_EQUAL(index, settings.settings.equalizer);
        CHECK(strcmp(panel.model.detail(&panel, EQUALIZER_ROW),
                     i18n_text(EQUALIZER_PRESET_NAMES[index])) == 0);
    }
    open_values(EQUALIZER_ROW);
    CHECK_EQUAL(EQUALIZER_PRESET_COUNT + 1, panel.values.model.count(&panel.values));
    CHECK(value_label_is(EQUALIZER_PRESET_COUNT, equalizer_presets_slot_label(5)));
    pick(EQUALIZER_ROW, EQUALIZER_PRESET_COUNT);
    CHECK_EQUAL(EQUALIZER_CUSTOM, settings.settings.equalizer);
    CHECK(memcmp(gains, settings.settings.equalizer_gains, sizeof gains) == 0);
    CHECK(strcmp(panel.model.detail(&panel, EQUALIZER_ROW), equalizer_presets_slot_label(5)) == 0);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK(memcmp(gains, restored.settings.equalizer_gains, sizeof gains) == 0);
    settings_store_apply_hardware(&restored.settings);
    CHECK_EQUAL(SETTINGS_EQUALIZER_BANDS, fake_hal_audio_equalizer().band_count);
    pick(EQUALIZER_ROW, 0);
    CHECK_EQUAL(EQUALIZER_OFF, settings.settings.equalizer);
}

static void headphone_profiles_are_chosen_from_off_and_the_four_slots_and_survive_reload(void)
{
    start();
    open_values(HEADPHONES_ROW);
    CHECK_EQUAL(HEADPHONE_PROFILE_LIMIT + 1, panel.values.model.count(&panel.values));
    CHECK(value_label_is(0, i18n_text(STR_OFF)));
    CHECK_EQUAL(0, panel.values.list.selected);
    for (int slot = 0; slot < HEADPHONE_PROFILE_LIMIT; slot++) {
        pick(HEADPHONES_ROW, slot + 1);
        HeadphoneStore restored;
        headphone_store_load(&restored);
        CHECK_EQUAL(slot, restored.profiles.current);
        CHECK(strcmp(panel.model.detail(&panel, HEADPHONES_ROW),
                     restored.profiles.slots[slot].name) == 0);
        open_values(HEADPHONES_ROW);
        CHECK_EQUAL(slot + 1, panel.values.list.selected);
        CHECK(value_label_is(slot + 1, restored.profiles.slots[slot].name));
    }
    pick(HEADPHONES_ROW, 0);
    HeadphoneStore restored;
    headphone_store_load(&restored);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, restored.profiles.current);
    CHECK(row_detail_is(HEADPHONES_ROW, STR_OFF));
}

static void sleep_is_chosen_for_the_same_session_timer_and_does_not_persist(void)
{
    start();
    for (int option = 1; option < SLEEP_TIMER_OPTION_COUNT; option++) {
        pick(SLEEP_ROW, option);
        CHECK(power.sleep_timer_active);
        CHECK_EQUAL(settings_sleep_option_minutes(option) * 60000, power.sleep_duration_ms);
        CHECK_EQUAL(option, settings_action_current_sleep(&panel.config.settings));
    }
    SettingsContext other = panel.config.settings;
    settings_action_choose_sleep(&other, 1);
    CHECK_EQUAL(1, settings_action_current_sleep(&panel.config.settings));
    open_values(SLEEP_ROW);
    CHECK_EQUAL(1, panel.values.list.selected);
    pick(SLEEP_ROW, 2);
    CHECK_EQUAL(2, settings_action_current_sleep(&other));
    settings_action_choose_sleep(&other, 0);
    CHECK(!power.sleep_timer_active);
    CHECK_EQUAL(0, fake_hal_write_count());
}

static void speed_is_listed_only_while_playback_can_change_speed(void)
{
    start();
    CHECK(strcmp(panel.model.label(&panel, SPEED_ROW), i18n_text(STR_SPEED)) == 0);
    CHECK(row_detail_is(SPEED_ROW, STR_SPEED_100));
    pick(SPEED_ROW, SETTINGS_SPEED_NORMAL + 1);
    CHECK_EQUAL(SETTINGS_SPEED_NORMAL + 1, settings.settings.playback_speed);
    CHECK(row_detail_is(SPEED_ROW, SETTINGS_SPEED_LABELS[SETTINGS_SPEED_NORMAL + 1]));
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(SETTINGS_SPEED_NORMAL + 1, restored.settings.playback_speed);
    panel.config.settings.player = NULL;
    CHECK_EQUAL(ROWS - 1, panel.model.count(&panel));
    CHECK(strcmp(panel.model.label(&panel, SPEED_ROW), i18n_text(STR_SLEEP_TIMER)) == 0);
    panel.config.settings.player = &player;
    CHECK_EQUAL(ROWS, panel.model.count(&panel));
}

static void losing_speed_support_keeps_the_selection_inside_the_panel(void)
{
    start();
    list_view_select(&panel.list, UP_NEXT_ROW, 100);
    panel.config.settings.player = NULL;
    panel.screen.update(&panel, 200);
    CHECK_EQUAL(UP_NEXT_ROW - 1, panel.list.selected);
    CHECK(press(KEY_PLAY).target == &up_next);
}

static void replay_gain_and_transitions_update_the_player_and_keep_other_settings(void)
{
    start();
    settings.settings.prevent_clipping = false;
    settings.settings.transition_tenths = 27;
    for (int mode = 1; mode <= REPLAY_GAIN_MODE_COUNT; mode++) {
        pick(REPLAY_GAIN_ROW, mode % REPLAY_GAIN_MODE_COUNT);
        CHECK_EQUAL(mode % REPLAY_GAIN_MODE_COUNT, player.replay_gain_mode);
        CHECK(!player.prevent_clipping);
        SettingsStore restored;
        settings_store_load(&restored);
        CHECK_EQUAL(player.replay_gain_mode, restored.settings.replay_gain_mode);
        CHECK(!restored.settings.prevent_clipping);
    }
    choose(TRANSITIONS_ROW);
    CHECK(player.fade_transitions);
    CHECK_EQUAL(27, player.transition_tenths);
    choose(TRANSITIONS_ROW);
    CHECK(!player.fade_transitions);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(TRACK_TRANSITION_OFF, restored.settings.track_transition);
    CHECK_EQUAL(27, restored.settings.transition_tenths);
}

static bool memory_matches_storage(void)
{
    SettingsStore restored;
    settings_store_load(&restored);
    const Settings *stored = &restored.settings;
    const Settings *shown = &settings.settings;
    return stored->equalizer == shown->equalizer &&
           stored->playback_speed == shown->playback_speed &&
           stored->replay_gain_mode == shown->replay_gain_mode &&
           stored->track_transition == shown->track_transition &&
           settings_now_playing_style(stored) == settings_now_playing_style(shown);
}

static void check_failed_save(int row, int value, bool cycle)
{
    start();
    char before[64];
    snprintf(before, sizeof before, "%s", panel.model.detail(&panel, row));
    Player player_before = player;
    fake_hal_fail_internal_writes(SETTINGS_STORE_PATH);
    if (cycle) {
        choose(row);
    } else {
        pick(row, value);
    }
    CHECK(strcmp(toast.text, i18n_text(STR_SETTINGS_NOT_SAVED)) == 0);
    CHECK(strcmp(before, panel.model.detail(&panel, row)) == 0);
    CHECK(memory_matches_storage());
    CHECK_EQUAL(player_before.shuffle, player.shuffle);
    CHECK_EQUAL(player_before.repeat, player.repeat);
    CHECK_EQUAL(player_before.replay_gain_mode, player.replay_gain_mode);
    CHECK_EQUAL(player_before.fade_transitions, player.fade_transitions);
    CHECK_EQUAL(player_before.speed, player.speed);
}

static void a_failed_save_leaves_every_saving_row_at_its_stored_value(void)
{
    check_failed_save(SHUFFLE_ROW, 0, true);
    check_failed_save(REPEAT_ROW, 0, true);
    check_failed_save(TRANSITIONS_ROW, 0, true);
    check_failed_save(EQUALIZER_ROW, EQUALIZER_RETRO, false);
    check_failed_save(SPEED_ROW, SETTINGS_SPEED_NORMAL + 1, false);
    check_failed_save(REPLAY_GAIN_ROW, REPLAY_GAIN_ALBUM, false);
    check_failed_save(STYLE_ROW, NOW_PLAYING_VU, false);
    start();
    fake_hal_fail_internal_writes(HEADPHONE_STORE_PATH);
    pick(HEADPHONES_ROW, 1);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, headphones.profiles.current);
    CHECK(strcmp(toast.text, i18n_text(STR_SETTINGS_NOT_SAVED)) == 0);
}

static void a_saved_transition_still_explains_shuffle(void)
{
    start();
    choose(TRANSITIONS_ROW);
    CHECK(player.fade_transitions);
    CHECK(strcmp(toast.text, i18n_text(STR_TRANSITIONS_IN_SHUFFLE)) == 0);
    CHECK(memory_matches_storage());
}

static void a_rebuild_after_the_slot_file_fails_clamps_the_equalizer_list(void)
{
    start();
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {2, -2, 4, -4, 6, -6, 8, -8, 10, -10};
    CHECK(equalizer_presets_save_slot(equalizer.presets, 2, gains));
    CHECK(equalizer_presets_save_slot(equalizer.presets, 5, gains));
    panel.screen.enter(&panel, 100);
    open_values(EQUALIZER_ROW);
    CHECK_EQUAL(EQUALIZER_PRESET_COUNT + 2, panel.values.model.count(&panel.values));
    list_view_select(&panel.values.list, EQUALIZER_PRESET_COUNT + 1, 100);
    uint8_t junk[EQUALIZER_SLOTS_FILE_SIZE];
    memset(junk, 0xA5, sizeof junk);
    CHECK(hal_internal_file_write(EQUALIZER_SLOTS_PATH, junk, sizeof junk));
    *equalizer.presets = (EqualizerPresets){0};
    equalizer_presets_scan(equalizer.presets);
    quick_settings_screen_refit(&panel);
    int count = panel.values.model.count(&panel.values);
    CHECK_EQUAL(EQUALIZER_PRESET_COUNT, count);
    CHECK(panel.values.list.selected < count);
    for (int value = 0; value < count; value++) {
        CHECK(panel.values.model.label(&panel.values, value) != NULL);
    }
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    panel.values.screen.draw(&panel.values, &canvas);
    int chosen = panel.values.list.selected;
    CHECK_EQUAL(SCREEN_ACTION_POP, press_values(KEY_PLAY).type);
    CHECK_EQUAL(chosen, settings.settings.equalizer);
}

static void a_rebuild_keeps_the_headphone_names(void)
{
    start();
    open_values(HEADPHONES_ROW);
    quick_settings_screen_refit(&panel);
    CHECK_EQUAL(HEADPHONE_PROFILE_LIMIT + 1, panel.values.model.count(&panel.values));
    CHECK(value_label_is(0, i18n_text(STR_OFF)));
    for (int value = 1; value <= HEADPHONE_PROFILE_LIMIT; value++) {
        CHECK(panel.values.model.label(&panel.values, value) != NULL);
        CHECK(panel.values.model.label(&panel.values, value)[0] != '\0');
    }
}

static void a_b_repeat_sets_a_then_b_then_clears(void)
{
    start();
    player.loaded = true;
    CHECK(strcmp(i18n_text(STR_AB_LOOP), panel.model.label(&panel, AB_ROW)) == 0);
    CHECK(row_detail_is(AB_ROW, STR_OFF));
    choose(AB_ROW);
    CHECK_EQUAL(1, player.ab_repeat);
    CHECK(row_detail_is(AB_ROW, STR_AB_ROW_A));
    choose(AB_ROW);
    CHECK_EQUAL(2, player.ab_repeat);
    CHECK(row_detail_is(AB_ROW, STR_AB_ROW_LOOP));
    choose(AB_ROW);
    CHECK_EQUAL(0, player.ab_repeat);
    CHECK(row_detail_is(AB_ROW, STR_OFF));
    CHECK_EQUAL('\0', toast.text[0]);
}

static void a_b_with_b_before_a_keeps_a_and_says_why(void)
{
    start();
    player.loaded = true;
    choose(AB_ROW);
    fake_hal_audio_refuse_repeat_b(true);
    choose(AB_ROW);
    CHECK_EQUAL(1, player.ab_repeat);
    CHECK(row_detail_is(AB_ROW, STR_AB_ROW_A));
    CHECK(strcmp(toast.text, i18n_text(STR_AB_B_AFTER_A)) == 0);
    fake_hal_audio_refuse_repeat_b(false);
    choose(AB_ROW);
    CHECK_EQUAL(2, player.ab_repeat);
}

static void a_b_without_a_track_stays_off(void)
{
    start();
    choose(AB_ROW);
    CHECK_EQUAL(0, player.ab_repeat);
    CHECK(row_detail_is(AB_ROW, STR_OFF));
}

static NowPlayingStyle saved_style(void)
{
    SettingsStore restored;
    settings_store_load(&restored);
    return settings_now_playing_style(&restored.settings);
}

static void now_playing_style_is_chosen_from_its_submenu_and_persists(void)
{
    start();
    CHECK(strcmp(i18n_text(STR_NOW_PLAYING), panel.model.label(&panel, STYLE_ROW)) == 0);
    CHECK(row_detail_is(STYLE_ROW, STR_NOW_PLAYING_COVER));
    static const NowPlayingStyle order[] = {NOW_PLAYING_VU, NOW_PLAYING_SPECTRUM,
                                            NOW_PLAYING_CASSETTE, NOW_PLAYING_COVER};
    for (int step = 0; step < 4; step++) {
        pick(STYLE_ROW, (int)order[step]);
        CHECK_EQUAL(order[step], settings_now_playing_style(&settings.settings));
        CHECK_EQUAL(order[step], saved_style());
        CHECK(row_detail_is(STYLE_ROW, STYLE_VALUES[order[step]]));
    }
    CHECK_EQUAL(SHUFFLE_OFF, player.shuffle);
    CHECK_EQUAL('\0', toast.text[0]);
}

static void the_style_follows_the_settings_choice_both_ways(void)
{
    start();
    SettingsContext other = panel.config.settings;
    settings_action_choose_now_playing(&other, NOW_PLAYING_VU);
    CHECK_EQUAL(NOW_PLAYING_VU, saved_style());
    CHECK(row_detail_is(STYLE_ROW, STR_NOW_PLAYING_VU));
    pick(STYLE_ROW, NOW_PLAYING_SPECTRUM);
    CHECK_EQUAL(NOW_PLAYING_SPECTRUM, settings_action_current_now_playing(&other));
    settings_action_choose_now_playing(&other, NOW_PLAYING_CASSETTE);
    CHECK(row_detail_is(STYLE_ROW, STR_NOW_PLAYING_CASSETTE));
    open_values(STYLE_ROW);
    CHECK_EQUAL(NOW_PLAYING_CASSETTE, panel.values.list.selected);
}

static void a_failed_style_save_keeps_the_stored_style(void)
{
    start();
    fake_hal_fail_internal_writes(SETTINGS_STORE_PATH);
    pick(STYLE_ROW, NOW_PLAYING_CASSETTE);
    CHECK_EQUAL(NOW_PLAYING_COVER, settings_now_playing_style(&settings.settings));
    CHECK_EQUAL(NOW_PLAYING_COVER, saved_style());
    CHECK(row_detail_is(STYLE_ROW, STR_NOW_PLAYING_COVER));
    CHECK(strcmp(toast.text, i18n_text(STR_SETTINGS_NOT_SAVED)) == 0);
}

static void set_layout(int orientation, int size)
{
    theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
}

static Canvas full_canvas(void)
{
    return canvas_make(pixels,
                       (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
}

static const FooterHint QUICK_SETTINGS_HINTS[] = {{FOOTER_KEY_PLAY, STR_HINT_CHANGE},
                                                  {FOOTER_KEY_BACK, STR_HINT_CLOSE}};

static void all_rows_fit_every_theme_orientation_and_text_size(void)
{
    start();
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                set_layout(orientation, size);
                default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
                panel.screen.enter(&panel, 100);
                CHECK(panel.list.visible_rows > 0);
                FooterHints footer = {QUICK_SETTINGS_HINTS, 2, FOOTER_ALIGN_END};
                CHECK_EQUAL(theme_is_portrait(&theme) ? 48 : 24,
                            footer_hints_height(&theme, footer));
                for (int row = 0; row < panel.model.count(&panel); row++) {
                    list_view_select(&panel.list, row, 100);
                    Canvas canvas = full_canvas();
                    text_spy_reset();
                    panel.screen.draw(&panel, &canvas);
                    CHECK(panel.list.top <= row);
                    CHECK(panel.list.top + panel.list.visible_rows > row);
                }
            }
        }
    }
}

static bool shown_whole(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        const TextSpyEntry *entry = text_spy_entry(index);
        if (strcmp(entry->shown, text) == 0 && strcmp(entry->rest, text) == 0) {
            return true;
        }
    }
    return false;
}

static void the_a_b_row_fits_every_text_size_and_orientation(void)
{
    static const StringId values[] = {STR_OFF, STR_AB_ROW_A, STR_AB_ROW_LOOP};
    start();
    player.loaded = true;
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            for (int state = 0; state < 3; state++) {
                set_layout(orientation, size);
                panel.screen.enter(&panel, 100);
                player.ab_repeat = (uint8_t)state;
                list_view_select(&panel.list, AB_ROW, 100);
                Canvas canvas = full_canvas();
                text_spy_reset();
                panel.screen.draw(&panel, &canvas);
                CHECK(shown_whole(i18n_text(STR_AB_LOOP)));
                CHECK(shown_whole(i18n_text(values[state])));
                screen_snapshot_write(&canvas, "quick-settings-ab",
                                      (orientation * TEXT_SIZE_COUNT + size) * 3 + state);
            }
        }
    }
}

static void the_style_row_fits_every_text_size_and_orientation(void)
{
    start();
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            for (int style = 0; style < 4; style++) {
                set_layout(orientation, size);
                panel.screen.enter(&panel, 100);
                settings_set_now_playing_style(&settings.settings, (NowPlayingStyle)style);
                list_view_select(&panel.list, STYLE_ROW, 100);
                Canvas canvas = full_canvas();
                text_spy_reset();
                panel.screen.draw(&panel, &canvas);
                CHECK(shown_whole(i18n_text(STR_NOW_PLAYING)));
                CHECK(shown_whole(i18n_text(STYLE_VALUES[style])));
                screen_snapshot_write(&canvas, "quick-settings-style",
                                      (orientation * TEXT_SIZE_COUNT + size) * 4 + style);
            }
        }
    }
}

static void check_submenu_fits(size_t submenu, int layout)
{
    open_values(SUBMENU_ROWS[submenu]);
    int count = panel.values.model.count(&panel.values);
    for (int value = count - 1; value >= 0; value--) {
        list_view_select(&panel.values.list, value, 100);
        Canvas canvas = full_canvas();
        text_spy_reset();
        panel.values.screen.draw(&panel.values, &canvas);
        CHECK(shown_whole(panel.values.screen.title));
        CHECK(shown_whole(panel.values.model.label(&panel.values, value)));
        CHECK(panel.values.list.top <= value);
        CHECK(panel.values.list.top + panel.values.list.visible_rows > value);
        if (value == 0) {
            screen_snapshot_write(&canvas, SUBMENU_NAMES[submenu], layout);
        }
    }
}

static void every_row_label_and_value_is_shown_whole_at_every_size_and_orientation(void)
{
    start();
    player.loaded = true;
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            set_layout(orientation, size);
            panel.screen.enter(&panel, 100);
            for (int row = ROWS - 1; row >= 0; row--) {
                list_view_select(&panel.list, row, 100);
                Canvas canvas = full_canvas();
                text_spy_reset();
                panel.screen.draw(&panel, &canvas);
                CHECK(shown_whole(panel.model.label(&panel, row)));
                const char *value = panel.model.detail(&panel, row);
                CHECK(value == NULL || shown_whole(value));
            }
        }
    }
}

static void every_submenu_fits_every_text_size_and_orientation(void)
{
    start();
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {1};
    CHECK(equalizer_presets_save_slot(panel.config.settings.presets, 0, gains));
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            set_layout(orientation, size);
            panel.screen.enter(&panel, 100);
            for (size_t submenu = 0; submenu < sizeof SUBMENU_ROWS / sizeof SUBMENU_ROWS[0];
                 submenu++) {
                check_submenu_fits(submenu, orientation * TEXT_SIZE_COUNT + size);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(navigation_wraps_and_back_closes_without_changing_a_value),
        TEST_CASE(rows_with_four_or_more_values_open_a_submenu_and_the_rest_act_in_place),
        TEST_CASE(cycle_rows_wrap_to_their_first_value),
        TEST_CASE(a_submenu_marks_and_highlights_the_current_value_on_entry),
        TEST_CASE(cancelling_any_submenu_leaves_every_value_unchanged),
        TEST_CASE(every_eq_preset_and_used_slot_can_be_chosen_and_persists),
        TEST_CASE(headphone_profiles_are_chosen_from_off_and_the_four_slots_and_survive_reload),
        TEST_CASE(sleep_is_chosen_for_the_same_session_timer_and_does_not_persist),
        TEST_CASE(speed_is_listed_only_while_playback_can_change_speed),
        TEST_CASE(losing_speed_support_keeps_the_selection_inside_the_panel),
        TEST_CASE(replay_gain_and_transitions_update_the_player_and_keep_other_settings),
        TEST_CASE(a_failed_save_leaves_every_saving_row_at_its_stored_value),
        TEST_CASE(a_saved_transition_still_explains_shuffle),
        TEST_CASE(a_rebuild_after_the_slot_file_fails_clamps_the_equalizer_list),
        TEST_CASE(a_rebuild_keeps_the_headphone_names),
        TEST_CASE(a_b_repeat_sets_a_then_b_then_clears),
        TEST_CASE(a_b_with_b_before_a_keeps_a_and_says_why),
        TEST_CASE(a_b_without_a_track_stays_off),
        TEST_CASE(now_playing_style_is_chosen_from_its_submenu_and_persists),
        TEST_CASE(the_style_follows_the_settings_choice_both_ways),
        TEST_CASE(a_failed_style_save_keeps_the_stored_style),
        TEST_CASE(all_rows_fit_every_theme_orientation_and_text_size),
        TEST_CASE(the_a_b_row_fits_every_text_size_and_orientation),
        TEST_CASE(the_style_row_fits_every_text_size_and_orientation),
        TEST_CASE(every_row_label_and_value_is_shown_whole_at_every_size_and_orientation),
        TEST_CASE(every_submenu_fits_every_text_size_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
