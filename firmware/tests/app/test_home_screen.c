#include <stddef.h>
#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "app_screens.h"
#include "card_files.h"
#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"
#include "home_art.h"
#include "music_check.h"
#include "settings_store.h"
#include "spoken_library.h"
#include "ui/slide.h"

enum {
    START_MS = 1000,
    KEY_HOLD_MS = 30,
    LONG_HOLD_MS = 700,
    MUSIC = 0,
    AUDIOBOOKS = 1,
    PODCASTS = 2,
    SETTINGS = SETTINGS_HOME_SETTINGS_ITEM,
    NOW_PLAYING = 4,
    UP_NEXT = 5,
    MIXTAPES = 6,
    LISTENING = 7,
    USB_MODE = 8,
    CLOCK = 9,
    HOME_SETTINGS_ROW = 5,
    EDITOR_RESET_ROW = HOME_EDITOR_ROWS - 1
};

static AppScreens screens;
static LibraryStore store;
static Player player;
static CoverCache covers;
static FavoritesStore favorites;
static Toast toast;
static SettingsStore settings;
static PowerPolicy power;
static BluetoothLink bluetooth;
static AppOptions options;
static Theme styled;
static bool rescan_requested;
static bool language_requested;
static uint32_t now_ms;

static void use_order(Settings *chosen, const uint8_t *order, uint16_t hidden)
{
    memcpy(chosen->home_order, order, SETTINGS_HOME_ITEM_COUNT);
    chosen->home_hidden = hidden;
}

static void build(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_reset();
    fake_hal_add_file(LIBRARY_STORE_PATH, bytes, size);
    library_store_open(&store);
    styled = BOARD_THEME;
    player = (Player){.library = &store.library};
    screens = (AppScreens){.context = {.options = &options,
                                       .theme = &BOARD_THEME,
                                       .styled_theme = &styled,
                                       .compact_theme = &BOARD_THEME,
                                       .store = &store,
                                       .player = &player,
                                       .settings = &settings,
                                       .favorites = &favorites,
                                       .power = &power,
                                       .toast = &toast,
                                       .covers = &covers,
                                       .rescan_requested = &rescan_requested,
                                       .language_requested = &language_requested,
                                       .bluetooth = &bluetooth}};
    app_screens_init(&screens, screens.context);
}

static void build_with(const uint8_t *order, uint16_t hidden)
{
    settings.settings = settings_defaults();
    use_order(&settings.settings, order, hidden);
    build();
}

static int home_rows(void)
{
    return screens.home.model.count(&screens.home);
}

static bool home_row_is(int row, int item)
{
    const char *label = screens.home.model.label(&screens.home, row);
    return strcmp(label, i18n_text(APP_HOME_ITEMS[item].label)) == 0;
}

static bool selected_home_item_is(int item)
{
    return home_row_is(screens.home.list.selected, item);
}

static void the_default_home_has_six_rows_and_a_track_adds_the_first_row(void)
{
    static const int EXPECTED[] = {MUSIC, MIXTAPES, LISTENING, CLOCK, USB_MODE, SETTINGS};
    settings.settings = settings_defaults();
    build();
    CHECK_EQUAL(6, home_rows());
    for (int row = 0; row < 6; row++) {
        CHECK(home_row_is(row, EXPECTED[row]));
    }
    player.has_track = true;
    CHECK_EQUAL(7, home_rows());
    CHECK(home_row_is(0, NOW_PLAYING));
    CHECK(home_row_is(1, MUSIC));
    player.has_track = false;
    CHECK_EQUAL(6, home_rows());
    CHECK(home_row_is(0, MUSIC));
}

static void now_playing_ignores_a_saved_hide_bit_and_opens_the_existing_screen(void)
{
    static const uint8_t ORDER[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    build_with(ORDER, 1u << NOW_PLAYING);
    player.has_track = true;
    player.state = PLAYER_PAUSED;
    CHECK(home_row_is(0, NOW_PLAYING));
    ScreenAction action = screens.home.screen.handle_key(
        &screens.home, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS}, START_MS);
    CHECK(action.target == &screens.now_playing.screen);
    player.has_track = false;
    CHECK(home_row_is(0, MUSIC));
}

static void spoken_shelves_are_reachable_from_music_when_present(void)
{
    settings.settings = settings_defaults();
    build();
    CHECK_EQUAL(MUSIC_ITEM_COUNT, screens.music.model.count(&screens.music));
    SpokenLibrary spoken;
    store.library = *spoken_library_open(&spoken);
    store.available = true;
    app_screens_init(&screens, screens.context);
    CHECK_EQUAL(MUSIC_MENU_ITEM_COUNT, screens.music.model.count(&screens.music));
    CHECK_EQUAL(6, home_rows());
    for (int shelf = 0; shelf < 2; shelf++) {
        int row = MUSIC_ITEM_COUNT + shelf;
        StringId label = shelf == 0 ? STR_AUDIOBOOKS : STR_PODCASTS;
        CHECK(strcmp(i18n_text(label), screens.music.model.label(&screens.music, row)) == 0);
        list_view_select(&screens.music.list, row, 0);
        ScreenAction action = screens.music.screen.handle_key(
            &screens.music, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS}, START_MS);
        CHECK(action.target != NULL);
        CHECK(strcmp(i18n_text(label), action.target->title) == 0);
    }
    spoken_library_close(&spoken);
}

static void the_editor_omits_now_playing_and_marks_the_default_hidden_items(void)
{
    settings.settings = settings_defaults();
    build();
    HomeEditor *editor = &screens.modules.settings.home_editor;
    Screen *screen = home_editor_init(editor, (HomeEditorConfig){.settings = &settings.settings,
                                                                 .items = APP_HOME_ITEMS,
                                                                 .theme = &BOARD_THEME});
    screen->enter(editor, START_MS);
    CHECK_EQUAL(10, editor->model.count(editor));
    for (int row = 0; row < HOME_EDITOR_ITEM_COUNT; row++) {
        CHECK(strcmp(editor->model.label(editor, row), i18n_text(STR_NOW_PLAYING)) != 0);
        int item = settings.settings.home_order[row];
        StringId detail = item == SETTINGS ? STR_HOME_ALWAYS
                          : item == AUDIOBOOKS || item == PODCASTS || item == UP_NEXT
                              ? STR_HOME_HIDDEN
                              : STR_HOME_SHOWN;
        CHECK(strcmp(i18n_text(detail), editor->model.detail(editor, row)) == 0);
    }
}

static void home_lists_the_saved_order_without_the_hidden_items(void)
{
    static const uint8_t ORDER[] = {USB_MODE,   LISTENING, SETTINGS,    MIXTAPES, MUSIC,
                                    AUDIOBOOKS, PODCASTS,  NOW_PLAYING, UP_NEXT,  CLOCK};
    build_with(ORDER, 1u << LISTENING | 1u << MUSIC);
    CHECK_EQUAL(4, home_rows());
    CHECK(home_row_is(0, USB_MODE));
    CHECK(home_row_is(1, SETTINGS));
    CHECK(home_row_is(2, MIXTAPES));
}

static void now_playing_and_up_next_still_appear_only_with_a_track(void)
{
    static const uint8_t ORDER[] = {UP_NEXT,  NOW_PLAYING, MUSIC,     AUDIOBOOKS, PODCASTS,
                                    SETTINGS, MIXTAPES,    LISTENING, USB_MODE,   CLOCK};
    build_with(ORDER, 0);
    CHECK(home_row_is(0, MUSIC));
    player.has_track = true;
    CHECK(home_row_is(0, NOW_PLAYING));
    CHECK(home_row_is(1, UP_NEXT));
    CHECK(home_row_is(2, MUSIC));
}

static void a_shown_shelf_item_still_needs_its_shelf(void)
{
    static const uint8_t ORDER[] = {PODCASTS, AUDIOBOOKS, MUSIC,     SETTINGS, NOW_PLAYING,
                                    UP_NEXT,  MIXTAPES,   LISTENING, USB_MODE, CLOCK};
    build_with(ORDER, 0);
    CHECK(!screens.shelf_entries[0].present && !screens.shelf_entries[1].present);
    CHECK(home_row_is(0, MUSIC));
    CHECK_EQUAL(6, home_rows());
}

static void settings_stays_on_home_even_when_the_mask_names_it(void)
{
    static const uint8_t ORDER[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    build_with(ORDER, 0x3FF);
    CHECK_EQUAL(1, home_rows());
    CHECK(home_row_is(0, SETTINGS));
}

static void the_selected_item_keeps_the_selection_when_home_is_rearranged(void)
{
    static const uint8_t ORDER[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    static const uint8_t MOVED[] = {MIXTAPES,   USB_MODE, LISTENING,   MUSIC,   SETTINGS,
                                    AUDIOBOOKS, PODCASTS, NOW_PLAYING, UP_NEXT, CLOCK};
    build_with(ORDER, 0);
    list_view_select(&screens.home.list, 1, 0);
    CHECK(selected_home_item_is(SETTINGS));
    use_order(&settings.settings, MOVED, 1u << MUSIC);
    app_screens_arrange_home(&screens);
    CHECK_EQUAL(5, home_rows());
    CHECK_EQUAL(3, screens.home.list.selected);
    CHECK(selected_home_item_is(SETTINGS));
}

static void a_selected_item_that_is_hidden_gives_the_selection_to_the_first_row(void)
{
    static const uint8_t ORDER[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    build_with(ORDER, 0);
    list_view_select(&screens.home.list, 2, 0);
    CHECK(selected_home_item_is(MIXTAPES));
    settings.settings.home_hidden = 1u << MIXTAPES;
    app_screens_arrange_home(&screens);
    CHECK_EQUAL(0, screens.home.list.selected);
}

static void the_now_playing_start_screen_selects_now_playing_wherever_it_is(void)
{
    static const uint8_t ORDER[] = {MUSIC,    MIXTAPES, LISTENING,   SETTINGS, AUDIOBOOKS,
                                    PODCASTS, USB_MODE, NOW_PLAYING, UP_NEXT,  CLOCK};
    build_with(ORDER, 1u << LISTENING);
    player.has_track = true;
    Screen *path[4];
    CHECK_EQUAL(1, app_screens_start_path(&screens, START_NOW_PLAYING, path));
    CHECK(path[0] == &screens.now_playing.screen);
    CHECK(selected_home_item_is(NOW_PLAYING));
}

static void add_player_files(void)
{
    uint32_t size = 0;
    uint32_t covers_size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    const uint8_t *cover_bytes = fixture_covers_bytes(&covers_size);
    fake_hal_add_file(LIBRARY_STORE_PATH, bytes, size);
    fake_hal_add_file(COVER_CACHE_PATH, cover_bytes, covers_size);
    for (size_t index = 0; index < card_files_count(); index++) {
        HalFile file;
        if (card_files_slow(index) && !hal_file_open(&file, card_files_path(index))) {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
    fake_hal_add_file(MUSIC_CHECK_PATH, NULL, 4096);
    music_check_remember((MusicFingerprint){.complete = true});
}

static void start_app(void)
{
    fake_hal_reset();
    add_player_files();
    Settings defaults = settings_defaults();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&defaults, bytes);
    CHECK(hal_internal_file_write(SETTINGS_STORE_PATH, bytes, sizeof bytes));
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void step(void)
{
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void press(Key key, uint32_t duration_ms)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + duration_ms);
    now_ms += duration_ms + 10;
    step();
    now_ms += SLIDE_DURATION_MS;
    step();
}

static void tap(Key key)
{
    press(key, KEY_HOLD_MS);
}

static void taps(Key key, int times)
{
    for (int count = 0; count < times; count++) {
        tap(key);
    }
}

static bool title_is(StringId title)
{
    return strcmp(i18n_text(title), app_screen_title()) == 0;
}

static bool toast_is(const char *expected)
{
    return app_toast_text() != NULL && strcmp(expected, app_toast_text()) == 0;
}

static Settings saved_settings(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    Settings saved = settings_defaults();
    CHECK(bytes != NULL && settings_decode(bytes, size, &saved));
    return saved;
}

static void open_editor_row(int row)
{
    taps(KEY_VOLUME_UP, HOME_SETTINGS_ROW);
    tap(KEY_PLAY);
    CHECK(title_is(STR_SETTINGS));
    taps(KEY_VOLUME_DOWN, 4);
    tap(KEY_PLAY);
    CHECK(title_is(STR_HOME_SCREEN));
    if (row == EDITOR_RESET_ROW) {
        taps(KEY_VOLUME_UP, row);
        return;
    }
    Settings chosen = saved_settings();
    for (int index = 0; index < HOME_EDITOR_ITEM_COUNT; index++) {
        if (chosen.home_order[index] == row) {
            taps(KEY_VOLUME_UP, index);
            return;
        }
    }
}

static void go_home(void)
{
    press(KEY_BACK, LONG_HOLD_MS);
    CHECK_EQUAL(1, app_screen_depth());
}

static void hiding_an_item_in_the_editor_takes_it_off_home_and_saves(void)
{
    start_app();
    open_editor_row(MUSIC);
    tap(KEY_PLAY);
    CHECK(title_is(STR_MUSIC));
    tap(KEY_PLAY);
    CHECK(title_is(STR_HOME_SCREEN));
    CHECK(toast_is("Music hidden from Home"));
    CHECK_EQUAL(settings_defaults().home_hidden | 1u << MUSIC, saved_settings().home_hidden);
    go_home();
    tap(KEY_PLAY);
    CHECK(title_is(STR_SETTINGS));
}

static void a_hidden_item_offers_show_and_comes_back(void)
{
    start_app();
    open_editor_row(MIXTAPES);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(title_is(STR_MIXTAPES));
    tap(KEY_PLAY);
    CHECK(toast_is("Mixtapes shown on Home"));
    CHECK_EQUAL(settings_defaults().home_hidden, saved_settings().home_hidden);
}

static void holding_play_picks_an_item_up_and_volume_moves_it(void)
{
    static const uint8_t EXPECTED[] = {SETTINGS, MUSIC,      MIXTAPES, LISTENING, CLOCK,
                                       USB_MODE, AUDIOBOOKS, PODCASTS, UP_NEXT,   NOW_PLAYING};
    start_app();
    open_editor_row(SETTINGS);
    press(KEY_PLAY, LONG_HOLD_MS);
    CHECK(title_is(STR_HOME_SCREEN));
    taps(KEY_VOLUME_DOWN, 8);
    tap(KEY_PLAY);
    CHECK(toast_is("Settings placed"));
    Settings saved = saved_settings();
    CHECK(memcmp(EXPECTED, saved.home_order, sizeof EXPECTED) == 0);
    tap(KEY_BACK);
    tap(KEY_BACK);
    CHECK_EQUAL(1, app_screen_depth());
    tap(KEY_PLAY);
    CHECK(title_is(STR_SETTINGS));
}

static void a_moved_order_survives_a_restart(void)
{
    start_app();
    open_editor_row(SETTINGS);
    press(KEY_PLAY, LONG_HOLD_MS);
    taps(KEY_VOLUME_DOWN, 8);
    tap(KEY_PLAY);
    app_start();
    CHECK_EQUAL(1, app_screen_depth());
    tap(KEY_PLAY);
    CHECK(title_is(STR_SETTINGS));
}

static void back_cancels_a_move_and_keeps_the_saved_order(void)
{
    start_app();
    open_editor_row(USB_MODE);
    press(KEY_PLAY, LONG_HOLD_MS);
    taps(KEY_VOLUME_DOWN, 4);
    tap(KEY_BACK);
    CHECK(title_is(STR_HOME_SCREEN));
    CHECK(toast_is("Move cancelled"));
    CHECK_EQUAL(3, app_screen_depth());
    tap(KEY_PLAY);
    CHECK(title_is(STR_USB_MODE));
}

static void the_move_action_picks_the_item_up_like_a_hold(void)
{
    start_app();
    open_editor_row(LISTENING);
    tap(KEY_PLAY);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(title_is(STR_HOME_SCREEN));
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(toast_is("Stats placed"));
    CHECK_EQUAL(LISTENING, saved_settings().home_order[3]);
}

static void settings_offers_only_the_move_action(void)
{
    start_app();
    open_editor_row(SETTINGS);
    tap(KEY_PLAY);
    CHECK(title_is(STR_SETTINGS));
    tap(KEY_PLAY);
    CHECK(title_is(STR_HOME_SCREEN));
    tap(KEY_BACK);
    CHECK(toast_is("Move cancelled"));
}

static void leaving_during_a_move_discards_it(void)
{
    start_app();
    open_editor_row(MUSIC);
    press(KEY_PLAY, LONG_HOLD_MS);
    taps(KEY_VOLUME_UP, 3);
    go_home();
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(title_is(STR_MUSIC));
}

static void reset_shows_every_item_in_the_default_order(void)
{
    start_app();
    open_editor_row(MUSIC);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    press(KEY_PLAY, LONG_HOLD_MS);
    taps(KEY_VOLUME_UP, 4);
    tap(KEY_PLAY);
    taps(KEY_VOLUME_UP, EDITOR_RESET_ROW - 4);
    tap(KEY_PLAY);
    CHECK(toast_is("Home screen reset"));
    Settings saved = saved_settings();
    Settings defaults = settings_defaults();
    CHECK(memcmp(defaults.home_order, saved.home_order, sizeof saved.home_order) == 0);
    CHECK_EQUAL(defaults.home_hidden, saved.home_hidden);
    go_home();
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(title_is(STR_MUSIC));
}

static void the_reset_row_cannot_be_picked_up(void)
{
    start_app();
    open_editor_row(EDITOR_RESET_ROW);
    press(KEY_PLAY, LONG_HOLD_MS);
    tap(KEY_VOLUME_UP);
    tap(KEY_PLAY);
    CHECK(title_is(STR_MUSIC));
}

static void missing_cover_storage_does_not_walk_every_album(void)
{
    static FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    LibraryStore library_store = {.library = *library, .available = true};
    CoverCache cache = {.library = &library_store.library};
    HomeArt art;
    Player idle = {0};
    home_art_init(&art, &cache, &library_store, &idle, &BOARD_THEME);
    int before = fixture.memory.reads;
    home_art_update(&art, 1000);
    CHECK(fixture.memory.reads - before <= 8);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_default_home_has_six_rows_and_a_track_adds_the_first_row),
        TEST_CASE(now_playing_ignores_a_saved_hide_bit_and_opens_the_existing_screen),
        TEST_CASE(spoken_shelves_are_reachable_from_music_when_present),
        TEST_CASE(the_editor_omits_now_playing_and_marks_the_default_hidden_items),
        TEST_CASE(home_lists_the_saved_order_without_the_hidden_items),
        TEST_CASE(missing_cover_storage_does_not_walk_every_album),
        TEST_CASE(now_playing_and_up_next_still_appear_only_with_a_track),
        TEST_CASE(a_shown_shelf_item_still_needs_its_shelf),
        TEST_CASE(settings_stays_on_home_even_when_the_mask_names_it),
        TEST_CASE(the_selected_item_keeps_the_selection_when_home_is_rearranged),
        TEST_CASE(a_selected_item_that_is_hidden_gives_the_selection_to_the_first_row),
        TEST_CASE(the_now_playing_start_screen_selects_now_playing_wherever_it_is),
        TEST_CASE(hiding_an_item_in_the_editor_takes_it_off_home_and_saves),
        TEST_CASE(a_hidden_item_offers_show_and_comes_back),
        TEST_CASE(holding_play_picks_an_item_up_and_volume_moves_it),
        TEST_CASE(a_moved_order_survives_a_restart),
        TEST_CASE(back_cancels_a_move_and_keeps_the_saved_order),
        TEST_CASE(the_move_action_picks_the_item_up_like_a_hold),
        TEST_CASE(settings_offers_only_the_move_action),
        TEST_CASE(leaving_during_a_move_discards_it),
        TEST_CASE(reset_shows_every_item_in_the_default_order),
        TEST_CASE(the_reset_row_cannot_be_picked_up),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
