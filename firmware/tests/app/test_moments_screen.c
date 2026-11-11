#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "playback/moments.h"
#include "screens/moments_screen.h"

static FixtureLibrary fixture;
static Player player;
static MomentsScreen moments;
static Toast toast;
static Screen now_playing;
static Screen *screen;
static Theme theme;
static WireMotion motion;

static ScreenAction choose(int row, KeyGesture gesture)
{
    list_view_select(&moments.list, row, 100);
    return screen->handle_key(screen->context, (KeyEvent){.key = KEY_PLAY, .gesture = gesture},
                              100);
}

static void finish(void)
{
    for (int step = 0; step < 1000; step++) {
        screen->update(screen->context, 200);
    }
}

static void open_screen(void)
{
    screen = moments_screen_init(
        &moments, (MomentsScreenConfig){&player, &theme, &toast, &now_playing, &motion});
    screen->enter(screen->context, 0);
}

static void reset(void)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(HAL_RESERVE_CREATED);
    player_init(&player, fixture_library_open(&fixture));
    toast = (Toast){0};
    theme = BOARD_THEME;
    open_screen();
}

static void a_saved_moment_plays_its_exact_position_after_restart_and_rescan(void)
{
    reset();
    CHECK(
        player_play_list(&player, (BrowseQuery){.kind = BROWSE_SINGLE_TRACK, .subject = 0}, 0, 0));
    fake_hal_audio_set_position(12345);
    choose(0, KEY_GESTURE_PRESS);
    finish();
    CHECK(strcmp(toast.text, i18n_text(STR_MOMENT_SAVED)) == 0);
    CHECK_EQUAL(2, moments.model.count(&moments));
    LibrarySectionInfo tracks = fixture.library.sections[LIBRARY_TRACKS];
    uint8_t temporary[64];
    memcpy(temporary, fixture.bytes + tracks.offset, tracks.record_size);
    memcpy(fixture.bytes + tracks.offset, fixture.bytes + tracks.offset + tracks.record_size,
           tracks.record_size);
    memcpy(fixture.bytes + tracks.offset + tracks.record_size, temporary, tracks.record_size);
    player_release_output(&player);
    player_init(&player, &fixture.library);
    player_set_rewind(&player, true);
    open_screen();
    choose(1, KEY_GESTURE_PRESS);
    finish();
    CHECK_EQUAL(1, player.track);
    CHECK_EQUAL(PLAYER_PLAYING, player.state);
    CHECK_EQUAL(12345, player_status(&player).position_ms);
    CHECK_EQUAL(12345, fake_hal_audio_play_start());
    CHECK(moments.destination == &now_playing);
}

static void hold_delete_removes_only_the_chosen_moment_after_confirmation(void)
{
    reset();
    CHECK(
        player_play_list(&player, (BrowseQuery){.kind = BROWSE_SINGLE_TRACK, .subject = 0}, 0, 0));
    choose(0, KEY_GESTURE_PRESS);
    finish();
    choose(1, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(2, moments.model.count(&moments));
    choose(1, KEY_GESTURE_PRESS);
    finish();
    CHECK_EQUAL(1, moments.model.count(&moments));
    CHECK(strcmp(toast.text, i18n_text(STR_MOMENT_DELETED)) == 0);
    open_screen();
    CHECK_EQUAL(1, moments.model.count(&moments));
}

static void saving_without_a_track_gives_feedback_and_creates_no_file(void)
{
    reset();
    choose(0, KEY_GESTURE_PRESS);
    finish();
    CHECK(strcmp(toast.text, i18n_text(STR_NOTHING_PLAYING)) == 0);
    CHECK_EQUAL(0, fake_hal_reserved_size("/" FIRMWARE_DATA_DIRECTORY "/moments.bin"));
}

static void every_visible_moment_has_a_label_in_small_portrait(void)
{
    reset();
    default_theme_set_text_size(&theme, TEXT_SIZE_SMALL);
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    CHECK(player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 0, 0));
    open_screen();
    for (int index = 0; index < 16; index++) {
        choose(0, KEY_GESTURE_PRESS);
        finish();
    }
    for (int row = moments.list.top; row < moments.list.top + moments.list.visible_rows; row++) {
        CHECK(moments.model.label(&moments, row)[0] != 0);
    }
}

static uint32_t spoken_position(void *context, uint16_t track)
{
    (void)context;
    (void)track;
    return 12345;
}

static void a_moment_at_zero_overrides_a_spoken_bookmark_before_audio_starts(void)
{
    reset();
    LibraryTrack track;
    CHECK(library_read_track(&fixture.library, 0, &track));
    uint32_t path = fixture.library.sections[LIBRARY_STRINGS].offset + track.path;
    strcpy((char *)fixture.bytes + path, "Audiobooks/m.flac");
    player_set_resume(&player, (PlayerResume){spoken_position, NULL});
    CHECK(player_play_list(&player, browse_root_query(BROWSE_SINGLE_TRACK), 0, 0));
    fake_hal_audio_set_position(0);
    choose(0, KEY_GESTURE_PRESS);
    finish();
    player_release_output(&player);
    player_init(&player, &fixture.library);
    player_set_resume(&player, (PlayerResume){spoken_position, NULL});
    open_screen();
    choose(1, KEY_GESTURE_PRESS);
    finish();
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    CHECK_EQUAL(0, player_status(&player).position_ms);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_saved_moment_plays_its_exact_position_after_restart_and_rescan),
        TEST_CASE(hold_delete_removes_only_the_chosen_moment_after_confirmation),
        TEST_CASE(saving_without_a_track_gives_feedback_and_creates_no_file),
        TEST_CASE(every_visible_moment_has_a_label_in_small_portrait),
        TEST_CASE(a_moment_at_zero_overrides_a_spoken_bookmark_before_audio_starts),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
