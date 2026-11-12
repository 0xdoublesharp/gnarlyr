#include <string.h>

#include "check.h"
#include "core/calendar.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "listening_fixture.h"
#include "screen_snapshot.h"
#include "screens/listening_screen.h"
#include "screens/listening_work.h"
#include "screens/recap_cards.h"
#include "text_spy.h"

enum {
    DAY = 86400,
    HOUR = 3600,
    RECAP_ROW = 5,
    TICK_LIMIT = 2000,
    SETTLE_TICKS = 200,
    SAMPLE_PLAYS = 40,
    FULL_DECK = 8,
    LIGHT = 2 * TEXT_SIZE_COUNT
};

static FixtureLibrary fixture;
static ListeningDisk disk;
static ListeningStore store;
static ListeningScreen screen;
static CoverCache covers;
static Settings settings;
static Theme theme;
static WireMotion motion;
static Toast toast;
static bool scanning;
static uint32_t clock_ms;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static uint32_t day_of(uint16_t year, uint8_t month, uint8_t day)
{
    return calendar_days((CalendarDate){year, month, day, 0});
}

static uint32_t now_utc(void)
{
    return day_of(2026, 11, 11) * DAY + 15 * HOUR;
}

static void add_play(uint16_t index, uint32_t clock_s, int16_t offset, uint32_t listened_ms)
{
    ListeningTrack track;
    CHECK_EQUAL(STORE_READY, listening_track_read(&fixture.library, index, &track));
    ListeningPlay play = {.track = track,
                          .length_ms = 240000,
                          .listened_ms = listened_ms,
                          .end_ms = listened_ms,
                          .end = listened_ms < 120000 ? LISTENING_SKIPPED : LISTENING_FINISHED,
                          .output = (ListeningOutput)(index % LISTENING_OUTPUT_COUNT),
                          .format = (ListeningFormat)(index % LISTENING_FORMAT_COUNT),
                          .clock_s = clock_s,
                          .offset_minutes = offset};
    CHECK_EQUAL(STORE_DONE, listening_log_append(&store.log, &play));
}

static void add_sample(void)
{
    uint32_t first = day_of(2026, 11, 1);
    for (uint32_t index = 0; index < SAMPLE_PLAYS; index++) {
        uint16_t track = (uint16_t)((index * 3 + index / FIXTURE_TRACKS) % FIXTURE_TRACKS);
        uint32_t day = first + index * 10 / SAMPLE_PLAYS;
        uint32_t second = (uint32_t)(18 + index % 5) * HOUR + index * 60;
        add_play(track, day * DAY + second, 60, 150000 + track * 20000u);
    }
    add_play(1, day_of(2026, 10, 20) * DAY + 9 * HOUR, 60, 200000);
}

static void start(bool sample, uint32_t utc_s, int16_t offset)
{
    fake_hal_reset();
    clock_ms = 1000;
    fake_hal_set_milliseconds(clock_ms);
    fixture_library_open(&fixture);
    uint32_t size = 0;
    const uint8_t *bytes = fixture_covers_bytes(&size);
    fake_hal_add_file(COVER_CACHE_PATH, bytes, size);
    cover_cache_open(&covers, &fixture.library);
    listening_disk_reset(&disk);
    listening_store_attach(&store, &fixture.library, listening_disk_file(&disk, sizeof disk.bytes));
    fake_hal_set_wall(utc_s, offset);
    if (sample) {
        add_sample();
    }
    settings = settings_defaults();
    theme = BOARD_THEME;
    motion = (WireMotion){0};
    toast = (Toast){0};
    scanning = false;
    listening_screen_init(&screen, (ListeningScreenConfig){&store, &theme, &motion, &toast,
                                                           &scanning, &covers, &settings});
    screen.screen.enter(&screen, clock_ms);
}

static ScreenAction key(Key button)
{
    fake_hal_set_milliseconds(++clock_ms);
    return screen.screen.handle_key(
        &screen, (KeyEvent){.key = button, .gesture = KEY_GESTURE_PRESS}, clock_ms);
}

static void tick_by(uint32_t elapsed_ms)
{
    clock_ms += elapsed_ms;
    fake_hal_set_milliseconds(clock_ms);
    screen.screen.update(&screen, clock_ms);
}

static void select_row(int row)
{
    int rows = screen.model.count(&screen);
    for (int step = 0; step < rows && screen.list.selected != row; step++) {
        key(screen.list.selected < row ? KEY_VOLUME_UP : KEY_VOLUME_DOWN);
    }
    CHECK_EQUAL(row, screen.list.selected);
    key(KEY_PLAY);
}

static const RecapWork *recap_work(void)
{
    return &listening_module_work.as.recap;
}

static void settle(void)
{
    for (int step = 0; step < SETTLE_TICKS; step++) {
        tick_by(16);
    }
}

static void open_period(ListeningPeriod period)
{
    select_row(RECAP_ROW);
    CHECK_EQUAL(LISTENING_PERIOD_COUNT, screen.model.count(&screen));
    select_row((int)period);
    for (int step = 0; step < TICK_LIMIT; step++) {
        tick_by(16);
        if (recap_cards_count(&screen) > 0) {
            tick_by(16);
            return;
        }
        if (!motion.running) {
            return;
        }
    }
}

static Canvas draw(void)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    screen.screen.draw(&screen, &canvas);
    return canvas;
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

static bool saw_whole(StringId id)
{
    return saw(i18n_text(id));
}

static bool saw_start(StringId id)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->rest, i18n_text(id)) == 0) {
            return true;
        }
    }
    return false;
}

static void the_recap_row_offers_four_periods(void)
{
    start(true, now_utc(), 60);
    select_row(RECAP_ROW);
    draw();
    CHECK_EQUAL(LISTENING_PERIOD_COUNT, screen.model.count(&screen));
    CHECK(saw_whole(STR_RECAP_THIS_MONTH));
    CHECK(saw_whole(STR_RECAP_LAST_MONTH));
    CHECK(saw_whole(STR_RECAP_THIS_YEAR));
    CHECK(saw_whole(STR_RECAP_ALL_TIME));
    key(KEY_BACK);
    CHECK_EQUAL(RECAP_ROW, screen.list.selected);
}

static void the_cards_page_with_the_volume_keys_and_stop_at_the_ends(void)
{
    start(true, now_utc(), 60);
    open_period(LISTENING_THIS_MONTH);
    CHECK_EQUAL(FULL_DECK, recap_cards_count(&screen));
    CHECK_EQUAL(0, screen.card);
    key(KEY_VOLUME_DOWN);
    CHECK_EQUAL(0, screen.card);
    for (int press = 0; press < FULL_DECK + 2; press++) {
        key(KEY_VOLUME_UP);
    }
    CHECK_EQUAL(FULL_DECK - 1, screen.card);
    CHECK_EQUAL(0, strcmp(i18n_text(STR_RECAP_HIGHLIGHTS), screen.screen.title));
    key(KEY_VOLUME_DOWN);
    CHECK_EQUAL(0, strcmp(i18n_text(STR_RECAP_SOUND), screen.screen.title));
    key(KEY_PLAY);
    CHECK_EQUAL(FULL_DECK - 1, screen.card);
    key(KEY_BACK);
    CHECK_EQUAL(LISTENING_PERIOD_COUNT, screen.model.count(&screen));
    CHECK_EQUAL(LISTENING_THIS_MONTH, screen.list.selected);
}

static void the_title_card_flourish_runs_once_for_a_second_then_holds_still(void)
{
    start(true, now_utc(), 60);
    open_period(LISTENING_THIS_MONTH);
    CHECK(motion.running);
    CHECK_EQUAL(WIRE_SHAPE_ICOSAHEDRON, motion.shape);
    uint32_t started = motion.started_ms;
    tick_by(RECAP_FLOURISH_MS / 2);
    CHECK(motion.running);
    CHECK(motion.percent > 0 && motion.percent < 100);
    tick_by(RECAP_FLOURISH_MS / 2);
    CHECK(!motion.running);
    CHECK(recap_work()->flourished);
    CHECK_EQUAL(100, recap_work()->still.percent);
    CHECK(clock_ms - started >= RECAP_FLOURISH_MS);
    key(KEY_VOLUME_UP);
    key(KEY_VOLUME_DOWN);
    tick_by(16);
    CHECK(!motion.running);
    draw();
    CHECK(recap_work()->still.area.width >= WIRE_MOTION_MIN_SIDE);
}

static void the_clock_offset_puts_a_late_play_and_now_in_the_next_month(void)
{
    uint32_t midnight = day_of(2026, 11, 1) * DAY;
    start(false, midnight - 30 * 60, 60);
    add_play(0, midnight - 50 * 60, 60, 200000);
    add_play(1, midnight - 4 * HOUR, 60, 200000);
    open_period(LISTENING_THIS_MONTH);
    CHECK_EQUAL(1, recap_work()->job.recap.totals.plays);
    ListeningTrack first;
    listening_track_read(&fixture.library, 0, &first);
    CHECK_EQUAL(first.key, recap_work()->job.recap.top[2][0].track.key);
    key(KEY_BACK);
    fake_hal_set_wall(midnight - 30 * 60, 0);
    key(KEY_PLAY);
    tick_by(16);
    CHECK_EQUAL(1, recap_work()->job.recap.totals.plays);
    ListeningTrack second;
    listening_track_read(&fixture.library, 1, &second);
    CHECK_EQUAL(second.key, recap_work()->job.recap.top[2][0].track.key);
}

static void a_period_without_plays_and_an_unset_clock_explain_themselves(void)
{
    start(false, now_utc(), 0);
    open_period(LISTENING_THIS_MONTH);
    draw();
    CHECK(saw_start(STR_RECAP_EMPTY));
    CHECK(saw_start(STR_LISTENING_HINT));
    CHECK_EQUAL(0, recap_cards_count(&screen));
    key(KEY_BACK);
    select_row(LISTENING_ALL_TIME);
    settle();
    draw();
    CHECK(saw_start(STR_LISTENING_EMPTY));
    start(true, 0, 0);
    open_period(LISTENING_LAST_MONTH);
    draw();
    CHECK(saw_start(STR_TIME_UNKNOWN));
    CHECK(saw_start(STR_TIME_UNKNOWN_HINT));
    key(KEY_BACK);
    select_row(LISTENING_ALL_TIME);
    settle();
    CHECK(recap_cards_count(&screen) > 0);
}

static void a_second_opening_reads_the_saved_recap_until_a_play_is_logged(void)
{
    start(true, now_utc(), 60);
    open_period(LISTENING_THIS_YEAR);
    uint32_t size = 0;
    CHECK(fake_hal_file_bytes(LISTENING_RECAP_PATH, &size) != NULL);
    CHECK_EQUAL(LISTENING_RECAP_FILE_BYTES, size);
    uint64_t listened = recap_work()->job.recap.totals.listened_ms;
    key(KEY_BACK);
    disk.reads = 0;
    key(KEY_PLAY);
    settle();
    CHECK_EQUAL(0, disk.reads);
    CHECK_EQUAL(listened, recap_work()->job.recap.totals.listened_ms);
    key(KEY_BACK);
    add_play(2, now_utc() - HOUR, 60, 100000);
    key(KEY_PLAY);
    settle();
    CHECK(disk.reads > 0);
    CHECK_EQUAL(listened + 100000, recap_work()->job.recap.totals.listened_ms);
}

static void a_recap_rebuilds_after_another_screen_reuses_the_work_buffer(void)
{
    start(true, now_utc(), 60);
    open_period(LISTENING_THIS_MONTH);
    key(KEY_VOLUME_UP);
    key(KEY_VOLUME_UP);
    listening_module_work.owner = NULL;
    memset(&listening_module_work.as, 0, sizeof listening_module_work.as);
    for (int step = 0; step < TICK_LIMIT && recap_cards_count(&screen) == 0; step++) {
        tick_by(16);
    }
    CHECK_EQUAL(FULL_DECK, recap_cards_count(&screen));
    CHECK_EQUAL(2, screen.card);
    draw();
    CHECK(saw_whole(STR_TOP_ARTISTS));
}

static void a_failed_history_read_is_reported(void)
{
    start(true, now_utc(), 60);
    disk.refuse_reads = true;
    open_period(LISTENING_ALL_TIME);
    draw();
    CHECK(saw_whole(STR_HISTORY_UNAVAILABLE));
    CHECK_EQUAL(0, recap_cards_count(&screen));
}

static uint8_t card_log[LISTENING_LOG_FILE_BYTES];

static void a_log_on_the_card_is_read_through_one_open_file_per_update(void)
{
    start(false, now_utc(), 60);
    fake_hal_add_file(LISTENING_LOG_PATH, card_log, sizeof card_log);
    listening_store_load(&store, &fixture.library);
    CHECK(store.file.path != NULL);
    add_sample();
    open_period(LISTENING_THIS_MONTH);
    CHECK_EQUAL(FULL_DECK, recap_cards_count(&screen));
    CHECK_EQUAL(SAMPLE_PLAYS, recap_work()->job.recap.totals.plays);
    CHECK(store.log.file.source.read == recap_work()->log.file.source.read);
}

static void check_card(int card)
{
    static const StringId TITLES[FULL_DECK] = {
        STR_RECAP,     STR_TOTAL_LISTENING, STR_TOP_ARTISTS, STR_TOP_ALBUMS,
        STR_TOP_SONGS, STR_RECAP_RHYTHM,    STR_RECAP_SOUND, STR_RECAP_HIGHLIGHTS};
    CHECK(saw_whole(TITLES[card]));
    CHECK((card >= 2 && card <= 4) || card == FULL_DECK - 1 || !saw("\xE2\x80\xA6"));
    if (card == 0) {
        CHECK(saw_whole(STR_RECAP_YOUR_MONTH));
        CHECK(saw_whole(STR_RECAP_HINT));
    } else if (card == 1) {
        CHECK(saw_whole(STR_LISTENING_PLAYS) && saw_whole(STR_ARTISTS));
    } else if (card == 5) {
        CHECK(saw_whole(STR_RECAP_DAY) && saw_whole(STR_RECAP_HOUR));
        CHECK(saw_whole(STR_RECAP_STREAK) && saw_whole(STR_RECAP_NEW_ARTISTS));
    } else if (card == 6) {
        CHECK(saw_whole(STR_RECAP_WIRED) && saw_whole(STR_BLUETOOTH));
        CHECK(saw_whole(STR_USB_DAC_TITLE) && saw_whole(STR_RECAP_HIRES));
    } else if (card == FULL_DECK - 1) {
        CHECK(saw_whole(STR_RECAP_TOP_ARTIST) && saw_whole(STR_RECAP_TOP_SONG));
    }
}

static void every_card_renders_whole_at_every_size_and_orientation(void)
{
    for (int variant = 0; variant < 2 * LIGHT; variant++) {
        start(true, now_utc(), 60);
        default_theme_set_text_size(&theme, (TextSize)(variant % TEXT_SIZE_COUNT));
        default_theme_set_orientation(&theme, (Orientation)(variant % LIGHT / TEXT_SIZE_COUNT));
        if (variant >= LIGHT) {
            default_theme_set_appearance(&theme, APPEARANCE_CLASSIC);
        }
        open_period(LISTENING_THIS_MONTH);
        settle();
        CHECK_EQUAL(FULL_DECK, recap_cards_count(&screen));
        for (int card = 0; card < FULL_DECK; card++) {
            Canvas canvas = draw();
            check_card(card);
            char name[32];
            StringBuilder builder = string_builder_make(name, sizeof name);
            string_builder_append(&builder, "recap-card");
            string_builder_append_unsigned(&builder, (uint32_t)card);
            screen_snapshot_write(&canvas, name, variant);
            key(KEY_VOLUME_UP);
        }
    }
}

static void the_empty_and_unset_clock_states_render_at_every_size_and_orientation(void)
{
    for (int variant = 0; variant < LIGHT; variant++) {
        for (int state = 0; state < 2; state++) {
            start(false, state == 0 ? now_utc() : 0, 0);
            default_theme_set_text_size(&theme, (TextSize)(variant % TEXT_SIZE_COUNT));
            default_theme_set_orientation(&theme, (Orientation)(variant / TEXT_SIZE_COUNT));
            open_period(LISTENING_THIS_MONTH);
            Canvas canvas = draw();
            CHECK(saw_start(state == 0 ? STR_RECAP_EMPTY : STR_TIME_UNKNOWN));
            CHECK(!saw("\xE2\x80\xA6"));
            screen_snapshot_write(&canvas, state == 0 ? "recap-empty" : "recap-no-clock", variant);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_recap_row_offers_four_periods),
        TEST_CASE(the_cards_page_with_the_volume_keys_and_stop_at_the_ends),
        TEST_CASE(the_title_card_flourish_runs_once_for_a_second_then_holds_still),
        TEST_CASE(the_clock_offset_puts_a_late_play_and_now_in_the_next_month),
        TEST_CASE(a_period_without_plays_and_an_unset_clock_explain_themselves),
        TEST_CASE(a_second_opening_reads_the_saved_recap_until_a_play_is_logged),
        TEST_CASE(a_recap_rebuilds_after_another_screen_reuses_the_work_buffer),
        TEST_CASE(a_failed_history_read_is_reported),
        TEST_CASE(a_log_on_the_card_is_read_through_one_open_file_per_update),
        TEST_CASE(every_card_renders_whole_at_every_size_and_orientation),
        TEST_CASE(the_empty_and_unset_clock_states_render_at_every_size_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
