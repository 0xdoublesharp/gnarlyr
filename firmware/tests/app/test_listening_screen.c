#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "listening_fixture.h"
#include "screen_snapshot.h"
#include "screens/listening_screen.h"
#include "text_spy.h"

static FixtureLibrary fixture;
static ListeningDisk disk;
static ListeningStore store;
static ListeningScreen screen;
static Theme theme;
static uint32_t clock_ms;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static void add_play(uint16_t index, bool known)
{
    ListeningTrack track;
    CHECK_EQUAL(STORE_READY, listening_track_read(&fixture.library, index, &track));
    if (!known) {
        track.key ^= 1;
    }
    ListeningPlay play = {.track = track,
                          .length_ms = 60000,
                          .listened_ms = 60000,
                          .end_ms = 60000,
                          .end = LISTENING_FINISHED,
                          .output = LISTENING_OUTPUT_SINGLE_ENDED,
                          .format = LISTENING_FORMAT_LOSSLESS};
    CHECK_EQUAL(STORE_DONE, listening_log_append(&store.log, &play));
}

static void start(bool populated)
{
    fake_hal_reset();
    fixture_library_open(&fixture);
    listening_disk_reset(&disk);
    listening_store_attach(&store, &fixture.library, listening_disk_file(&disk, sizeof disk.bytes));
    if (populated) {
        add_play(0, true);
        add_play(4, true);
    }
    theme = BOARD_THEME;
    clock_ms = 0;
    listening_screen_init(&screen, (ListeningScreenConfig){&store, &theme});
    screen.screen.enter(&screen, clock_ms);
}

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->shown, text) == 0) {
            return true;
        }
    }
    return false;
}

static Canvas draw(void)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    screen.screen.draw(&screen, &canvas);
    return canvas;
}

static ScreenAction key(Key button, KeyGesture gesture)
{
    return screen.screen.handle_key(&screen, (KeyEvent){.key = button, .gesture = gesture},
                                    ++clock_ms);
}

static void choose(int row)
{
    int rows = screen.model.count(&screen);
    for (int step = 0; step < rows && screen.list.selected != row; step++) {
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    }
    CHECK_EQUAL(row, screen.list.selected);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
}

static void tick(void)
{
    screen.screen.update(&screen, ++clock_ms);
}

static void finish_top(void)
{
    for (int step = 0; step < 1000; step++) {
        tick();
        if (screen.model.count(&screen) > 0 && screen.model.label(&screen, 0)[0] != '\0') {
            return;
        }
    }
    CHECK(false);
}

static void finish_export(void)
{
    for (int step = 0; step < 10000; step++) {
        tick();
        if (step % 64 == 0) {
            draw();
            if (saw(i18n_text(STR_EXPORT_DONE)) || saw(i18n_text(STR_EXPORT_FAILED))) {
                return;
            }
        }
    }
    CHECK(false);
}

static void every_top_group_resolves_names_without_starting_playback(void)
{
    for (int group = 0; group < 3; group++) {
        start(true);
        choose(group);
        draw();
        CHECK(saw(i18n_text(STR_LISTENING_WORKING)));
        finish_top();
        CHECK(screen.model.count(&screen) > 0);
        CHECK(screen.model.label(&screen, 0)[0] != '\0');
        CHECK(screen.model.detail(&screen, 0)[0] != '\0');
        CHECK(!screen.model.opens(&screen, 0));
        CHECK_EQUAL(0, fake_hal_audio_play_count());
        CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
        CHECK_EQUAL(group, screen.list.selected);
        CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    }
}

static void empty_history_and_unknown_track_identities_are_distinct(void)
{
    start(false);
    choose(0);
    for (int step = 0; step < 8; step++) {
        tick();
    }
    draw();
    CHECK(saw(i18n_text(STR_LISTENING_EMPTY)));
    start(false);
    add_play(0, false);
    choose(0);
    finish_top();
    CHECK_EQUAL(1, screen.model.count(&screen));
    CHECK(strcmp(i18n_text(STR_HISTORY_UNAVAILABLE), screen.model.label(&screen, 0)) == 0);
}

static void a_history_read_error_is_visible_and_a_fresh_job_can_recover(void)
{
    start(true);
    disk.refuse_reads = true;
    choose(0);
    tick();
    draw();
    CHECK(saw(i18n_text(STR_HISTORY_UNAVAILABLE)));
    key(KEY_BACK, KEY_GESTURE_PRESS);
    disk.refuse_reads = false;
    choose(0);
    finish_top();
    CHECK(strcmp(i18n_text(STR_HISTORY_UNAVAILABLE), screen.model.label(&screen, 0)) != 0);
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
}

static void exporting_history_writes_a_scrobbler_file_without_changing_the_log(void)
{
    start(true);
    choose(4);
    finish_export();
    CHECK(saw(i18n_text(STR_EXPORT_DONE)));
    CHECK_EQUAL(1, fake_hal_reserve_count());
    uint32_t bytes = 0;
    const uint8_t *text = fake_hal_file_bytes(LISTENING_EXPORT_PATH, &bytes);
    static const char HEADER[] = "#AUDIOSCROBBLER/1.1\n";
    CHECK(text != NULL);
    CHECK_EQUAL(LISTENING_EXPORT_FILE_BYTES, bytes);
    CHECK(memcmp(text, HEADER, sizeof HEADER - 1) == 0);
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
    CHECK_EQUAL(0, fake_hal_audio_play_count());
    for (int step = 0; step < 20; step++) {
        tick();
    }
    CHECK_EQUAL(1, fake_hal_reserve_count());
}

static void reservation_and_write_failures_are_shown_without_changing_history(void)
{
    for (int failure = 0; failure < 2; failure++) {
        start(true);
        if (failure == 0) {
            fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
        } else {
            fake_hal_fail_file_writes(LISTENING_EXPORT_PATH);
        }
        choose(4);
        finish_export();
        CHECK(saw(i18n_text(STR_EXPORT_FAILED)));
        CHECK_EQUAL(2, store.log.root.counters.totals.plays);
        CHECK(fake_hal_audio_service_count() > 0);
    }
}

static void backing_out_before_export_measurement_does_not_create_a_file(void)
{
    start(true);
    choose(4);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    for (int step = 0; step < 20; step++) {
        tick();
    }
    CHECK_EQUAL(0, fake_hal_reserve_count());
    CHECK_EQUAL(4, screen.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, key(KEY_BACK, KEY_GESTURE_LONG_PRESS).type);
}

static void a_job_rebuilds_after_another_screen_reuses_the_cold_work_buffer(void)
{
    start(true);
    choose(0);
    finish_top();
    int count = screen.model.count(&screen);
    ListeningScreen other;
    listening_screen_init(&other, (ListeningScreenConfig){&store, &theme});
    other.screen.enter(&other, 0);
    finish_top();
    CHECK_EQUAL(count, screen.model.count(&screen));
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
}

static void totals_and_top_lists_render_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start(true);
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            choose(3);
            Canvas canvas = draw();
            CHECK(saw(i18n_text(STR_TOTAL_LISTENING)));
            CHECK(saw(i18n_text(STR_LISTENING_PLAYS)));
            CHECK(saw(i18n_text(STR_LISTENING_SKIPS)));
            screen_snapshot_write(&canvas, "listening-totals",
                                  orientation * TEXT_SIZE_COUNT + size);
            key(KEY_BACK, KEY_GESTURE_PRESS);
            choose(0);
            finish_top();
            canvas = draw();
            CHECK(!saw(i18n_text(STR_HISTORY_UNAVAILABLE)));
            screen_snapshot_write(&canvas, "listening-top", orientation * TEXT_SIZE_COUNT + size);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_top_group_resolves_names_without_starting_playback),
        TEST_CASE(empty_history_and_unknown_track_identities_are_distinct),
        TEST_CASE(a_history_read_error_is_visible_and_a_fresh_job_can_recover),
        TEST_CASE(exporting_history_writes_a_scrobbler_file_without_changing_the_log),
        TEST_CASE(reservation_and_write_failures_are_shown_without_changing_history),
        TEST_CASE(backing_out_before_export_measurement_does_not_create_a_file),
        TEST_CASE(a_job_rebuilds_after_another_screen_reuses_the_cold_work_buffer),
        TEST_CASE(totals_and_top_lists_render_at_every_size_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
