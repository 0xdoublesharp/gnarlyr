#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "favorites_store.h"
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

static WireMotion motion;
static Toast toast;
static bool scanning;

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
    toast = (Toast){0};
    scanning = false;
    listening_screen_init(
        &screen, (ListeningScreenConfig){&store, &theme, &motion, &toast, &scanning, NULL, NULL});
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

static bool saw_whole(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->rest, text) == 0) {
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

static bool saw_export_state(void)
{
    return saw(i18n_text(STR_EXPORT_DONE)) || saw(i18n_text(STR_EXPORT_FAILED)) ||
           saw(i18n_text(STR_EXPORT_NONE));
}

static int finish_export(void)
{
    for (int step = 1; step <= 2000; step++) {
        tick();
        draw();
        if (saw_export_state()) {
            return step;
        }
    }
    CHECK(false);
    return 0;
}

static const char *formatted(StringId pattern, uint32_t count, const char *text)
{
    static char shown[256];
    char number[16];
    StringBuilder digits = string_builder_make(number, sizeof number);
    string_builder_append_unsigned(&digits, count);
    const char *arguments[] = {text == NULL ? number : text, LISTENING_EXPORT_PATH};
    StringBuilder builder = string_builder_make(shown, sizeof shown);
    i18n_format(&builder, pattern, arguments, 2);
    return shown;
}

static const char *saved_text(uint32_t plays)
{
    return formatted(plays == 1 ? STR_EXPORT_ONE_SAVED : STR_EXPORT_SAVED, plays, NULL);
}

static bool saw_progress_between_the_ends(void)
{
    for (uint32_t percent = 1; percent < 100; percent++) {
        if (saw(formatted(STR_EXPORT_PROGRESS, percent, NULL))) {
            return true;
        }
    }
    return false;
}

static bool valid_utf8(const uint8_t *text, size_t length)
{
    size_t index = 0;
    while (index < length) {
        uint8_t lead = text[index];
        size_t extra = lead < 0x80             ? 0
                       : (lead & 0xe0) == 0xc0 ? 1
                       : (lead & 0xf0) == 0xe0 ? 2
                       : (lead & 0xf8) == 0xf0 ? 3
                                               : 4;
        if (extra == 4 || (lead >= 0x80 && lead < 0xc2) || index + extra >= length + (extra == 0)) {
            return false;
        }
        for (size_t next = 1; next <= extra; next++) {
            if ((text[index + next] & 0xc0) != 0x80) {
                return false;
            }
        }
        index += extra + 1;
    }
    return true;
}

static bool digits_only(const char *field, size_t length, bool may_be_empty)
{
    if (length == 0) {
        return may_be_empty;
    }
    for (size_t index = 0; index < length; index++) {
        if (field[index] < '0' || field[index] > '9') {
            return false;
        }
    }
    return true;
}

static size_t check_record(const char *line, size_t left)
{
    const char *fields[8];
    size_t lengths[8];
    size_t count = 0;
    size_t at = 0;
    fields[0] = line;
    while (at < left && line[at] != '\n' && count < 8) {
        if (line[at] == '\t') {
            lengths[count] = (size_t)(line + at - fields[count]);
            count++;
            fields[count < 8 ? count : 7] = line + at + 1;
        }
        CHECK(line[at] != '\0' && line[at] != '\r');
        at++;
    }
    CHECK(at < left && line[at] == '\n');
    CHECK_EQUAL(7, (int)count);
    lengths[7] = (size_t)(line + at - fields[7]);
    CHECK(line[0] != '#' && lengths[0] > 0 && lengths[2] > 0);
    CHECK(digits_only(fields[3], lengths[3], true));
    CHECK(digits_only(fields[4], lengths[4], false));
    CHECK(lengths[5] == 1 && (fields[5][0] == 'L' || fields[5][0] == 'S'));
    CHECK(digits_only(fields[6], lengths[6], false));
    CHECK(lengths[7] == 0 || lengths[7] == 36);
    return at + 1;
}

static void check_scrobbler_file(uint32_t plays)
{
    static const char *const HEADERS[] = {"#AUDIOSCROBBLER/1.1\n", "#TZ/UTC\n",
                                          "#CLIENT/" FIRMWARE_NAME " " FIRMWARE_VERSION "\n"};
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(LISTENING_EXPORT_PATH, &size);
    const char *text = (const char *)bytes;
    CHECK(text != NULL && size > 0);
    if (text == NULL) {
        return;
    }
    CHECK(valid_utf8(bytes, size));
    size_t at = 0;
    for (size_t index = 0; index < 3; index++) {
        size_t length = strlen(HEADERS[index]);
        CHECK(memcmp(text + at, HEADERS[index], length) == 0);
        at += length;
    }
    uint32_t lines = 0;
    while (at < size) {
        at += check_record(text + at, size - at);
        lines++;
    }
    CHECK_EQUAL(size, (uint32_t)at);
    CHECK_EQUAL(plays, lines);
}

static bool export_file_exists(void)
{
    uint32_t size = 0;
    return fake_hal_file_bytes(LISTENING_EXPORT_PATH, &size) != NULL || size != 0;
}

static void fill_history(void)
{
    for (uint32_t play = 0; play < LISTENING_LOG_CAPACITY; play++) {
        add_play(play % 2 == 0 ? 0 : 4, true);
    }
}

static void tick_until_writing(void)
{
    for (int step = 0; step < 200 && fake_hal_reserve_count() == 0; step++) {
        tick();
    }
    CHECK_EQUAL(1, fake_hal_reserve_count());
    tick();
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

static void an_empty_history_says_there_is_nothing_to_export_and_writes_no_file(void)
{
    start(false);
    choose(4);
    CHECK(finish_export() <= 2);
    CHECK(saw(i18n_text(STR_EXPORT_NONE)));
    CHECK(saw_whole(i18n_text(STR_LISTENING_HINT)));
    CHECK(!saw(i18n_text(STR_BACK_CANCELS)));
    CHECK_EQUAL(0, fake_hal_reserve_count());
    CHECK(!export_file_exists());
}

static void plays_of_songs_gone_from_the_library_are_explained_instead_of_exported(void)
{
    start(false);
    add_play(0, false);
    choose(4);
    finish_export();
    CHECK(saw(i18n_text(STR_EXPORT_NONE)));
    CHECK(saw_whole(i18n_text(STR_EXPORT_UNKNOWN)));
    CHECK(!export_file_exists());
}

static void the_exported_file_ends_after_its_last_play_and_follows_the_scrobbler_format(void)
{
    start(true);
    choose(4);
    finish_export();
    CHECK(saw(i18n_text(STR_EXPORT_DONE)));
    CHECK(saw_whole(saved_text(2)));
    CHECK(!saw(i18n_text(STR_BACK_CANCELS)));
    check_scrobbler_file(2);
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
    CHECK_EQUAL(0, fake_hal_audio_play_count());
    for (int step = 0; step < 20; step++) {
        tick();
    }
    CHECK_EQUAL(1, fake_hal_reserve_count());
    start(false);
    add_play(0, true);
    choose(4);
    finish_export();
    CHECK(saw_whole(saved_text(1)));
    check_scrobbler_file(1);
}

static void an_existing_export_file_of_another_size_is_replaced_whole(void)
{
    static uint8_t stale[64 * 1024];
    static const uint32_t SIZES[] = {10, sizeof stale, LISTENING_EXPORT_FILE_BYTES};
    memset(stale, 'x', sizeof stale);
    for (size_t index = 0; index < sizeof SIZES / sizeof SIZES[0]; index++) {
        start(true);
        fake_hal_add_file(LISTENING_EXPORT_PATH, SIZES[index] <= sizeof stale ? stale : NULL,
                          SIZES[index]);
        choose(4);
        finish_export();
        CHECK(saw(i18n_text(STR_EXPORT_DONE)));
        check_scrobbler_file(2);
    }
}

static void a_full_history_exports_in_few_updates_and_shows_its_progress(void)
{
    start(false);
    fill_history();
    choose(4);
    draw();
    CHECK(saw(i18n_text(STR_EXPORT_WORKING)));
    CHECK(saw(i18n_text(STR_BACK_CANCELS)));
    tick();
    draw();
    CHECK(saw_progress_between_the_ends());
    CHECK(finish_export() < 160);
    CHECK(saw_whole(saved_text(store.log.root.kept)));
    check_scrobbler_file(store.log.root.kept);
}

static void storage_and_write_failures_say_what_went_wrong_without_changing_history(void)
{
    for (int failure = 0; failure < 4; failure++) {
        start(true);
        if (failure == 0) {
            fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
        } else if (failure == 1) {
            fake_hal_fail_file_writes(LISTENING_EXPORT_PATH);
        } else if (failure == 2) {
            fake_hal_add_file(LISTENING_EXPORT_PATH, NULL, 10);
            fake_hal_fail_file_writes(LISTENING_EXPORT_PATH);
        } else {
            disk.refuse_reads = true;
        }
        choose(4);
        finish_export();
        disk.refuse_reads = false;
        CHECK(saw(i18n_text(STR_EXPORT_FAILED)));
        StringId reason = failure == 0 ? STR_STORAGE_FULL : STR_HISTORY_UNAVAILABLE;
        CHECK(failure == 1 || failure == 2
                  ? saw_whole(formatted(STR_EXPORT_WRITE_FAILED, 0, LISTENING_EXPORT_PATH))
                  : saw_whole(i18n_text(reason)));
        CHECK_EQUAL(2, store.log.root.counters.totals.plays);
        CHECK(fake_hal_audio_service_count() > 0);
    }
    uint32_t size = 0;
    fake_hal_file_bytes(LISTENING_EXPORT_PATH, &size);
    CHECK_EQUAL(0, size);
}

static void backing_out_before_export_measurement_does_not_create_a_file(void)
{
    start(false);
    fill_history();
    choose(4);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    for (int step = 0; step < 20; step++) {
        tick();
    }
    CHECK_EQUAL(0, fake_hal_reserve_count());
    CHECK_EQUAL(4, screen.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, key(KEY_BACK, KEY_GESTURE_LONG_PRESS).type);
}

static void leaving_while_the_file_is_written_removes_the_partial_file(void)
{
    for (int home = 0; home < 2; home++) {
        start(false);
        fill_history();
        choose(4);
        tick_until_writing();
        CHECK(export_file_exists());
        ScreenAction action = key(KEY_BACK, home ? KEY_GESTURE_LONG_PRESS : KEY_GESTURE_PRESS);
        CHECK_EQUAL(home ? SCREEN_ACTION_POP_TO_ROOT : SCREEN_ACTION_NONE, action.type);
        CHECK(!export_file_exists());
        if (!home) {
            CHECK_EQUAL(4, screen.list.selected);
            CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
        }
    }
}

static void play_on_the_export_screen_neither_restarts_nor_stacks_it(void)
{
    start(true);
    choose(4);
    for (int press = 0; press < 4; press++) {
        CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_PLAY, KEY_GESTURE_PRESS).type);
    }
    finish_export();
    for (int press = 0; press < 4; press++) {
        CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_PLAY, KEY_GESTURE_PRESS).type);
        tick();
    }
    draw();
    CHECK(saw(i18n_text(STR_EXPORT_DONE)));
    CHECK_EQUAL(1, fake_hal_reserve_count());
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(4, screen.list.selected);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void choosing_export_again_writes_the_current_history(void)
{
    start(true);
    choose(4);
    finish_export();
    key(KEY_BACK, KEY_GESTURE_PRESS);
    add_play(0, true);
    choose(4);
    finish_export();
    CHECK(saw_whole(saved_text(3)));
    check_scrobbler_file(3);
}

static void an_export_restarted_after_its_buffer_was_reused_still_writes_a_whole_file(void)
{
    start(false);
    fill_history();
    choose(4);
    tick_until_writing();
    ListeningScreen other;
    listening_screen_init(
        &other, (ListeningScreenConfig){&store, &theme, &motion, &toast, &scanning, NULL, NULL});
    other.screen.enter(&other, 0);
    finish_export();
    CHECK(saw(i18n_text(STR_EXPORT_DONE)));
    check_scrobbler_file(store.log.root.kept);
}

static void a_job_rebuilds_after_another_screen_reuses_the_cold_work_buffer(void)
{
    start(true);
    choose(0);
    finish_top();
    int count = screen.model.count(&screen);
    ListeningScreen other;
    listening_screen_init(
        &other, (ListeningScreenConfig){&store, &theme, &motion, &toast, &scanning, NULL, NULL});
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

static void show_export_state(int state)
{
    start(state != 2);
    if (state == 0) {
        fill_history();
    } else if (state == 3) {
        fake_hal_fail_file_writes(LISTENING_EXPORT_PATH);
    }
    choose(4);
    if (state == 0) {
        tick();
        wire_motion_tick(&motion, motion.started_ms + WIRE_MOTION_DELAY_MS + 400);
    } else {
        finish_export();
    }
}

static void export_states_render_whole_at_every_size_and_orientation(void)
{
    static const char *const NAMES[] = {"working", "done", "none", "failed"};
    static const StringId HEADLINES[] = {STR_EXPORT_WORKING, STR_EXPORT_DONE, STR_EXPORT_NONE,
                                         STR_EXPORT_FAILED};
    for (int state = 0; state < 4; state++) {
        for (int variant = 0; variant < 2 * TEXT_SIZE_COUNT; variant++) {
            show_export_state(state);
            default_theme_set_text_size(&theme, (TextSize)(variant % TEXT_SIZE_COUNT));
            default_theme_set_orientation(&theme, (Orientation)(variant / TEXT_SIZE_COUNT));
            Canvas canvas = draw();
            CHECK(saw_whole(i18n_text(HEADLINES[state])));
            CHECK(!saw("\xE2\x80\xA6"));
            CHECK(state != 0 || saw(i18n_text(STR_BACK_CANCELS)));
            CHECK_EQUAL(state == 0, wire_motion_shown(&motion, &screen));
            CHECK(state != 0 || motion.area.width >= WIRE_MOTION_MIN_SIDE);
            char name[32];
            StringBuilder builder = string_builder_make(name, sizeof name);
            string_builder_append(&builder, "listening-export-");
            string_builder_append(&builder, NAMES[state]);
            screen_snapshot_write(&canvas, name, variant);
            if (state == 0) {
                default_theme_set_appearance(&theme, APPEARANCE_SUNSET);
                canvas = draw();
                CHECK(saw_whole(i18n_text(HEADLINES[state])));
                screen_snapshot_write(&canvas, name, variant + 2 * TEXT_SIZE_COUNT);
            }
        }
    }
}

static void open_clear(void)
{
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    choose(0);
}

static void clearing_without_export_empties_totals_and_rankings(void)
{
    start(true);
    open_clear();
    choose(1);
    CHECK_EQUAL(0, listening_store_totals(&store).plays);
    CHECK_EQUAL(0, listening_store_totals(&store).listened_ms);
    CHECK_EQUAL(0, store.log.session.plays);
    CHECK_EQUAL(0, store.log.root.kept);
    CHECK(!export_file_exists());
    ListeningLog recovered;
    CHECK_EQUAL(STORE_READY, listening_log_open(&recovered, listening_disk_config(&disk)));
    CHECK_EQUAL(0, recovered.root.kept);
    choose(0);
    for (int step = 0; step < 100; step++) {
        tick();
        draw();
        if (saw(i18n_text(STR_LISTENING_EMPTY))) {
            return;
        }
    }
    CHECK(false);
}

static void export_first_keeps_a_complete_export_then_clears(void)
{
    start(true);
    open_clear();
    choose(0);
    for (int step = 0; step < 2000 && store.log.root.kept != 0; step++) {
        tick();
    }
    CHECK_EQUAL(0, store.log.root.kept);
    check_scrobbler_file(2);
}

static void failed_or_cancelled_export_first_preserves_history(void)
{
    start(true);
    fake_hal_fail_file_writes(LISTENING_EXPORT_PATH);
    open_clear();
    choose(0);
    finish_export();
    CHECK_EQUAL(2, store.log.root.kept);
    start(true);
    open_clear();
    choose(0);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    tick();
    CHECK_EQUAL(2, store.log.root.kept);
}

static void an_export_in_progress_cannot_open_clear_options(void)
{
    start(true);
    choose(4);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(2, store.log.root.kept);
    finish_export();
    CHECK_EQUAL(2, store.log.root.kept);
}

static void an_interrupted_clear_recovers_old_or_empty_history(void)
{
    for (uint32_t cut = 0; cut <= STORE_BLOCK_BYTES; cut++) {
        start(true);
        open_clear();
        disk.cut_write = disk.writes + 1;
        disk.cut_bytes = cut;
        choose(1);
        disk.cut_write = 0;
        ListeningLog recovered;
        CHECK_EQUAL(STORE_READY, listening_log_open(&recovered, listening_disk_config(&disk)));
        CHECK(recovered.root.kept == 0 || recovered.root.kept == 2);
        CHECK_EQUAL(recovered.root.kept, recovered.root.counters.totals.plays);
        ListeningSeen seen;
        CHECK_EQUAL(0, listening_fixture_read_all(&recovered, &seen));
        CHECK_EQUAL(recovered.root.kept, seen.count);
        CHECK_EQUAL(3, recovered.root.next);
    }
}

static void clearing_discards_an_active_play_and_a_pending_retry(void)
{
    start(true);
    store.following = true;
    store.active.listened_ms = 5000;
    store.waiting = true;
    store.pending = store.active;
    store.retry_ms = UINT32_MAX / 2;
    open_clear();
    choose(1);
    CHECK(!store.following);
    CHECK(!store.waiting);
    CHECK_EQUAL(0, listening_store_totals(&store).listened_ms);
    listening_store_follow(&store, (ListeningObservation){0}, UINT32_MAX / 2);
    CHECK_EQUAL(0, store.log.root.kept);
    add_play(0, true);
    CHECK_EQUAL(1, store.log.root.kept);
    CHECK_EQUAL(1, store.log.root.counters.totals.plays);
}

static void moments_favorites_and_an_existing_export_are_untouched(void)
{
    static const char MOMENTS_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/moments.bin";
    static const uint8_t SAVED[] = {1, 9, 2, 7, 3, 5};
    start(true);
    fake_hal_add_file(MOMENTS_PATH, SAVED, sizeof SAVED);
    fake_hal_add_file(FAVORITES_STORE_PATH, SAVED, sizeof SAVED);
    fake_hal_add_file(LISTENING_EXPORT_PATH, SAVED, sizeof SAVED);
    open_clear();
    choose(1);
    const char *paths[] = {MOMENTS_PATH, FAVORITES_STORE_PATH, LISTENING_EXPORT_PATH};
    for (size_t index = 0; index < sizeof paths / sizeof paths[0]; index++) {
        uint32_t size = 0;
        const uint8_t *bytes = fake_hal_file_bytes(paths[index], &size);
        CHECK_EQUAL(sizeof SAVED, size);
        CHECK(bytes != NULL && memcmp(bytes, SAVED, sizeof SAVED) == 0);
    }
    CHECK(toast.visible);
    CHECK(strcmp(toast.text, i18n_text(STR_HISTORY_CLEARED)) == 0);
}

static void a_pending_scan_refuses_clear_and_export_first(void)
{
    for (int choice = 0; choice < 2; choice++) {
        start(true);
        open_clear();
        scanning = true;
        choose(choice);
        tick();
        CHECK_EQUAL(2, store.log.root.kept);
        CHECK(!export_file_exists());
        CHECK(toast.visible);
        CHECK(strcmp(toast.text, i18n_text(STR_SCANNING)) == 0);
    }
}

static void a_clear_with_unreadable_readback_can_be_retried_safely(void)
{
    start(true);
    disk.blind_from_write = disk.writes + 1;
    open_clear();
    choose(1);
    CHECK(store.log.needs_reload);
    CHECK(strcmp(toast.text, i18n_text(STR_HISTORY_UNAVAILABLE)) == 0);
    disk.blind_from_write = 0;
    choose(1);
    CHECK_EQUAL(0, store.log.root.kept);
    CHECK(!store.log.needs_reload);
    add_play(4, true);
    CHECK_EQUAL(1, store.log.root.kept);
}

static void confirmation_renders_whole_at_every_size_and_orientation(void)
{
    for (int variant = 0; variant < 2 * TEXT_SIZE_COUNT; variant++) {
        start(true);
        default_theme_set_text_size(&theme, (TextSize)(variant % TEXT_SIZE_COUNT));
        default_theme_set_orientation(&theme, (Orientation)(variant / TEXT_SIZE_COUNT));
        open_clear();
        Canvas canvas = draw();
        CHECK(saw_whole(i18n_text(STR_CLEAR_HISTORY)));
        CHECK(saw_whole(i18n_text(STR_EXPORT_FIRST)));
        CHECK(saw_whole(i18n_text(STR_CLEAR)));
        CHECK(saw_whole(i18n_text(STR_CARD_FORMAT_CANCEL)));
        CHECK(!saw("\xE2\x80\xA6"));
        char shown[1024];
        StringBuilder builder = string_builder_make(shown, sizeof shown);
        for (int index = 0; index < text_spy_count(); index++) {
            string_builder_append(&builder, text_spy_entry(index)->shown);
            string_builder_append_char(&builder, ' ');
        }
        static const StringId DETAILS[] = {
            STR_LISTENING_TOTALS, STR_TOP_SONGS,   STR_TOP_ALBUMS,
            STR_TOP_ARTISTS,      STR_MOST_PLAYED, STR_RECENTLY_PLAYED,
            STR_EXPORT_PLAYS,     STR_MOMENTS,     STR_FAVORITES};
        for (size_t index = 0; index < sizeof DETAILS / sizeof DETAILS[0]; index++) {
            CHECK(strstr(shown, i18n_text(DETAILS[index])) != NULL);
        }
        screen_snapshot_write(&canvas, "clear-listening-confirm", variant);
    }
}

static void holding_play_opens_history_options_without_clearing(void)
{
    start(true);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(1, screen.model.count(&screen));
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(3, screen.model.count(&screen));
    CHECK_EQUAL(2, screen.list.selected);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(holding_play_opens_history_options_without_clearing),
        TEST_CASE(moments_favorites_and_an_existing_export_are_untouched),
        TEST_CASE(a_pending_scan_refuses_clear_and_export_first),
        TEST_CASE(a_clear_with_unreadable_readback_can_be_retried_safely),
        TEST_CASE(confirmation_renders_whole_at_every_size_and_orientation),
        TEST_CASE(an_interrupted_clear_recovers_old_or_empty_history),
        TEST_CASE(clearing_discards_an_active_play_and_a_pending_retry),
        TEST_CASE(clearing_without_export_empties_totals_and_rankings),
        TEST_CASE(export_first_keeps_a_complete_export_then_clears),
        TEST_CASE(failed_or_cancelled_export_first_preserves_history),
        TEST_CASE(an_export_in_progress_cannot_open_clear_options),
        TEST_CASE(every_top_group_resolves_names_without_starting_playback),
        TEST_CASE(empty_history_and_unknown_track_identities_are_distinct),
        TEST_CASE(a_history_read_error_is_visible_and_a_fresh_job_can_recover),
        TEST_CASE(an_empty_history_says_there_is_nothing_to_export_and_writes_no_file),
        TEST_CASE(plays_of_songs_gone_from_the_library_are_explained_instead_of_exported),
        TEST_CASE(the_exported_file_ends_after_its_last_play_and_follows_the_scrobbler_format),
        TEST_CASE(an_existing_export_file_of_another_size_is_replaced_whole),
        TEST_CASE(a_full_history_exports_in_few_updates_and_shows_its_progress),
        TEST_CASE(storage_and_write_failures_say_what_went_wrong_without_changing_history),
        TEST_CASE(backing_out_before_export_measurement_does_not_create_a_file),
        TEST_CASE(leaving_while_the_file_is_written_removes_the_partial_file),
        TEST_CASE(play_on_the_export_screen_neither_restarts_nor_stacks_it),
        TEST_CASE(choosing_export_again_writes_the_current_history),
        TEST_CASE(an_export_restarted_after_its_buffer_was_reused_still_writes_a_whole_file),
        TEST_CASE(a_job_rebuilds_after_another_screen_reuses_the_cold_work_buffer),
        TEST_CASE(totals_and_top_lists_render_at_every_size_and_orientation),
        TEST_CASE(export_states_render_whole_at_every_size_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
