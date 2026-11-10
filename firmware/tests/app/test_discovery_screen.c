#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "discovery_screen.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "gfx/text.h"
#include "settings/settings.h"
#include "ui/label.h"

static FixtureLibrary fixture;
static DiscoveryScreen discovery;
static Theme theme;
static uint8_t scratch_bytes[LIBRARY_VIEW_WORK_BYTES];
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static bool refuse_writes;
static uint32_t clock_ms;

static bool scratch_read(void *context, uint32_t offset, void *bytes, uint32_t count)
{
    (void)context;
    if (offset > sizeof scratch_bytes || count > sizeof scratch_bytes - offset) {
        return false;
    }
    memcpy(bytes, scratch_bytes + offset, count);
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t count)
{
    (void)context;
    if (refuse_writes || offset > sizeof scratch_bytes || count > sizeof scratch_bytes - offset) {
        return false;
    }
    memcpy(scratch_bytes + offset, bytes, count);
    return true;
}

static void start(void)
{
    fake_hal_reset();
    fixture_library_open(&fixture);
    theme = BOARD_THEME;
    refuse_writes = false;
    clock_ms = 0;
    discovery_screen_init(&discovery,
                          (DiscoveryScreenConfig){.library = &fixture.library,
                                                  .theme = &theme,
                                                  .scratch = {scratch_read, scratch_write, NULL,
                                                              sizeof scratch_bytes}});
}

static ScreenAction key(Key button, KeyGesture gesture)
{
    return discovery.screen.handle_key(&discovery, (KeyEvent){.key = button, .gesture = gesture},
                                       ++clock_ms);
}

static void tick(void)
{
    discovery.screen.update(&discovery, ++clock_ms);
}

static void complete(void)
{
    for (int step = 0; step < 20000; step++) {
        tick();
        LibraryViewState state = discovery_screen_state(&discovery);
        if (state != LIBRARY_VIEW_SCANNING && state != LIBRARY_VIEW_SORTING) {
            return;
        }
    }
    CHECK(false);
}

static void explore(void)
{
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
}

static void search_owns_menu_and_wake_without_changing_playback(void)
{
    start();
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), true);
    CHECK(discovery.screen.takes_every_key);
    CHECK(discovery.screen.stays_on_wake);
    CHECK_EQUAL(DISCOVERY_EDITOR, discovery.mode);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK(strcmp(discovery.editor.text, "a") == 0);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    CHECK(strcmp(discovery.editor.text, "") == 0);
    CHECK_EQUAL(DISCOVERY_EDITOR, discovery.mode);
    CHECK_EQUAL(0, fake_hal_audio_play_count());
}

static void playing_results_exposes_the_exact_transformed_order(void)
{
    start();
    explore();
    int count = discovery.model.count(&discovery);
    CHECK(count > 1);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    uint16_t expected;
    TrackOrderSource source = discovery_screen_order(&discovery);
    CHECK(source.read(source.context, 1, 1, &expected));
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_REQUEST_PLAY, discovery.request);
    CHECK_EQUAL(1, discovery.play_position);
    CHECK_EQUAL(0, fake_hal_audio_play_count());
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(DISCOVERY_REQUEST_CANCEL_PLAY, discovery.request);
    discovery_screen_playback_done(&discovery, false);
    CHECK_EQUAL(DISCOVERY_RESULTS, discovery.mode);
    CHECK_EQUAL(DISCOVERY_REQUEST_NONE, discovery.request);
}

static void selecting_a_sort_rebuilds_and_keeps_the_original_selected_track(void)
{
    start();
    explore();
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    uint16_t selected;
    TrackOrderSource source = discovery_screen_order(&discovery);
    CHECK(source.read(source.context, 1, 1, &selected));
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_SORT, discovery.mode);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(LIBRARY_VIEW_SORT_TITLE, discovery.query.sort);
    complete();
    source = discovery_screen_order(&discovery);
    uint16_t restored;
    CHECK(source.read(source.context, (uint16_t)discovery.list.selected, 1, &restored));
    CHECK_EQUAL(selected, restored);
}

static void filter_choices_change_the_query_and_reset_removes_every_constraint(void)
{
    start();
    explore();
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_FILTERS, discovery.mode);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_PICKER, discovery.mode);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(0, discovery.query.artist);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    complete();
    key(KEY_MENU, KEY_GESTURE_PRESS);
    for (int step = 0; step < 3; step++) {
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    }
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(LIBRARY_NONE, discovery.query.artist);
    CHECK_EQUAL(0, discovery.required_tags);
    CHECK_EQUAL(0, discovery.query.text[0]);
    CHECK_EQUAL(LIBRARY_VIEW_SORT_ORIGINAL, discovery.query.sort);
}

static void cancellation_and_failed_reads_do_not_offer_a_playable_partial_list(void)
{
    start();
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(LIBRARY_VIEW_CANCELLED, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    refuse_writes = true;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_WRITE_FAILED, discovery_screen_state(&discovery));
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_REQUEST_NONE, discovery.request);
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
}

static void options_back_and_background_cancellation_keep_a_completed_selection(void)
{
    start();
    explore();
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(1, discovery.list.selected);
    uint16_t count = discovery_screen_order(&discovery).count;
    discovery_screen_cancel(&discovery);
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
    CHECK_EQUAL(count, discovery_screen_order(&discovery).count);
}

static void deferred_tag_initialization_remains_a_pending_search(void)
{
    start();
    PersonalTags opening = {.load = STORE_EMPTY, .result = STORE_WORKING};
    DiscoveryScreenConfig config = discovery.config;
    config.tags = &opening;
    discovery_screen_init(&discovery, config);
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    CHECK_EQUAL(LIBRARY_VIEW_SCANNING, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    discovery_screen_cancel(&discovery);
    CHECK_EQUAL(LIBRARY_VIEW_CANCELLED, discovery_screen_state(&discovery));
}

static void search_work_stops_at_the_clock_budget_and_services_audio(void)
{
    start();
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    fake_hal_set_microseconds_per_read(3000);
    int services = fake_hal_audio_service_count();
    tick();
    CHECK_EQUAL(LIBRARY_VIEW_SCANNING, discovery_screen_state(&discovery));
    CHECK(fake_hal_audio_service_count() > services);
    fake_hal_set_microseconds_per_read(1);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
}

static LibraryViewAcceptance album_scope(void *context, uint16_t track, const LibraryTrack *record)
{
    (void)track;
    return record->album == *(const uint16_t *)context ? LIBRARY_VIEW_ACCEPT : LIBRARY_VIEW_REJECT;
}

static void source_scope_is_combined_with_filters_and_survives_reset(void)
{
    start();
    LibraryTrack track;
    CHECK(library_read_track(&fixture.library, 0, &track));
    uint16_t album = track.album;
    discovery.config.scope_accept = album_scope;
    discovery.config.scope_context = &album;
    discovery.config.scope_title = i18n_text(STR_FAVORITES);
    explore();
    CHECK(strcmp(discovery.screen.title, i18n_text(STR_FAVORITES)) == 0);
    TrackOrderSource source = discovery_screen_order(&discovery);
    uint16_t count = source.count;
    CHECK(count > 0);
    CHECK(count < browse_count(&fixture.library, browse_root_query(BROWSE_ALL_SONGS)));
    for (uint16_t index = 0; index < count; index++) {
        uint16_t id;
        CHECK(source.read(source.context, index, 1, &id));
        CHECK(library_read_track(&fixture.library, id, &track));
        CHECK_EQUAL(album, track.album);
    }
    discovery.query.year_min = UINT16_MAX;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    for (int index = 0; index < 3; index++) {
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    }
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    complete();
    CHECK_EQUAL(count, discovery_screen_order(&discovery).count);
    CHECK(discovery.config.scope_accept == album_scope);
}

static void personal_tag_filters_are_intersected_with_the_source_scope(void)
{
    start();
    discovery_screen_tags(&discovery, 0);
    tick();
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    for (int step = 0; step < 2000; step++) {
        tick();
    }
    CHECK_EQUAL(DISCOVERY_TAGS, discovery.mode);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    for (int step = 0; step < 2000; step++) {
        tick();
    }
    CHECK(discovery.model.detail(&discovery, 1) != NULL);
    LibraryTrack track;
    CHECK(library_read_track(&fixture.library, 0, &track));
    uint16_t album = track.album;
    discovery.config.scope_accept = album_scope;
    discovery.config.scope_context = &album;
    explore();
    discovery.required_tags = 1;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    TrackOrderSource source = discovery_screen_order(&discovery);
    CHECK_EQUAL(1, source.count);
    uint16_t id = LIBRARY_NONE;
    CHECK(source.read(source.context, 0, 1, &id));
    CHECK_EQUAL(0, id);
    album++;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
}

static Color expected_footer[HAL_DISPLAY_WIDTH * 20];

static void a_view_save_failure_is_visible_without_disabling_results(void)
{
    start();
    fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
    explore();
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    discovery.screen.draw(&discovery, &canvas);
    Rect footer = {0, theme.metrics.screen_height - 20, theme.metrics.screen_width, 20};
    Canvas expected = canvas_make(expected_footer, footer);
    canvas_fill_rect(&expected, footer, theme.colors.background);
    Rect text = {theme.metrics.padding, footer.y, footer.width - 2 * theme.metrics.padding,
                 footer.height};
    label_draw(&expected, (TextStyle){theme.fonts.small, theme.colors.text_secondary},
               (Label){text, i18n_text(STR_VIEW_SAVE_FAILED)});
    CHECK(memcmp(expected_footer, pixels + footer.y * footer.width, sizeof expected_footer) == 0);
    CHECK(discovery_screen_order(&discovery).count > 0);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_REQUEST_PLAY, discovery.request);
}

static void metadata_rows_expose_fields_and_a_full_width_path(void)
{
    start();
    discovery_screen_info(&discovery, 0);
    tick();
    CHECK_EQUAL(DISCOVERY_INFO, discovery.mode);
    CHECK(strstr(discovery.model.label(&discovery, 0), i18n_text(STR_TITLE)) != NULL);
    discovery.list.top = 8;
    tick();
    CHECK(strstr(discovery.model.label(&discovery, 8), i18n_text(STR_FILE_PATH)) != NULL);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void personal_tag_management_does_not_assign_a_nonexistent_track(void)
{
    start();
    discovery_screen_tags(&discovery, LIBRARY_NONE);
    tick();
    CHECK_EQUAL(DISCOVERY_TAGS, discovery.mode);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_EDITOR, discovery.mode);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    for (int step = 0; step < 2000 && discovery.mode != DISCOVERY_TAGS; step++) {
        tick();
    }
    CHECK_EQUAL(DISCOVERY_TAGS, discovery.mode);
    CHECK_EQUAL(2, discovery.model.count(&discovery));
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(DISCOVERY_TAG_ACTIONS, discovery.mode);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_TAG_DELETE, discovery.mode);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_TAGS, discovery.mode);
}

static void reinitializing_the_same_screen_discards_the_previous_volume_work(void)
{
    start();
    discovery_screen_tags(&discovery, LIBRARY_NONE);
    tick();
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    for (int step = 0; step < 2000; step++) {
        tick();
    }
    CHECK_EQUAL(2, discovery.model.count(&discovery));
    start();
    discovery_screen_tags(&discovery, LIBRARY_NONE);
    for (int step = 0; step < 20; step++) {
        tick();
    }
    CHECK_EQUAL(1, discovery.model.count(&discovery));
    CHECK_EQUAL(LIBRARY_VIEW_IDLE, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
}

static void cancelling_a_search_entry_returns_to_its_caller(void)
{
    start();
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), true);
    CHECK_EQUAL(SCREEN_ACTION_POP, key(KEY_BACK, KEY_GESTURE_PRESS).type);
}

static void text_entry_rejects_overflow_and_erases_whole_utf8_characters(void)
{
    DiscoveryText editor;
    discovery_text_begin(&editor, "4294967296", true);
    uint32_t number = 7;
    CHECK(!discovery_text_number(&editor, UINT32_MAX, &number));
    CHECK_EQUAL(7, number);
    discovery_text_begin(&editor, "4294967295", true);
    CHECK(discovery_text_number(&editor, UINT32_MAX, &number));
    CHECK_EQUAL(UINT32_MAX, number);
    discovery_text_begin(&editor, "caf\xc3\xa9", false);
    discovery_text_key(&editor, (KeyEvent){.key = KEY_MENU, .gesture = KEY_GESTURE_PRESS});
    CHECK(strcmp(editor.text, "caf") == 0);
}

static void native_layouts_keep_editor_hints_and_rows_inside_every_orientation(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            start();
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), true);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            discovery.screen.draw(&discovery, &canvas);
            int bottom = theme.metrics.header_height + theme.metrics.padding +
                         2 * theme.metrics.row_height +
                         4 * (font_line_height(theme.fonts.small) + 2);
            CHECK(bottom <= theme.metrics.screen_height);
            const StringId hints[] = {STR_QUERY_CHOOSE_HINT, STR_QUERY_ADD_HINT,
                                      STR_QUERY_ERASE_HINT, STR_QUERY_SEARCH_HINT};
            for (unsigned index = 0; index < sizeof hints / sizeof *hints; index++) {
                CHECK(text_width(theme.fonts.small, i18n_text(hints[index])) <=
                      theme.metrics.screen_width - 2 * theme.metrics.padding);
            }
            key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
            complete();
            discovery.screen.draw(&discovery, &canvas);
            CHECK(discovery.list.visible_rows > 0);
            CHECK(discovery.list.visible_rows * list_view_row_height(&discovery.list) +
                      theme.metrics.header_height + 40 <=
                  theme.metrics.screen_height);
        }
    }
}

static void begin_deferred_replacement(PersonalTags *tags)
{
    start();
    *tags = (PersonalTags){.load = STORE_EMPTY, .result = STORE_DONE};
    DiscoveryScreenConfig config = discovery.config;
    config.tags = tags;
    discovery_screen_init(&discovery, config);
    explore();
    CHECK(discovery_screen_order(&discovery).count > 0);
    tags->result = STORE_WORKING;
    discovery.query.year_min = UINT16_MAX;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    CHECK_EQUAL(LIBRARY_VIEW_SCANNING, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
}

static void backing_out_of_a_deferred_replacement_never_restores_stale_results(void)
{
    PersonalTags tags;
    begin_deferred_replacement(&tags);
    CHECK_EQUAL(SCREEN_ACTION_NONE, key(KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    CHECK(discovery_screen_state(&discovery) != LIBRARY_VIEW_READY);
}

static void cancelling_a_deferred_replacement_never_exposes_the_previous_order(void)
{
    PersonalTags tags;
    begin_deferred_replacement(&tags);
    discovery_screen_cancel(&discovery);
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    CHECK(discovery_screen_state(&discovery) != LIBRARY_VIEW_READY);
}

static void leaving_home_during_tag_loading_cannot_republish_an_old_order(void)
{
    PersonalTags tags;
    begin_deferred_replacement(&tags);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, key(KEY_BACK, KEY_GESTURE_LONG_PRESS).type);
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    CHECK(discovery.destination == NULL);
}

static void a_cancelled_deferred_replacement_can_start_a_fresh_search(void)
{
    PersonalTags tags;
    begin_deferred_replacement(&tags);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    tags.result = STORE_DONE;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    discovery.query.year_min = 0;
    explore();
    CHECK(discovery_screen_order(&discovery).count > 0);
}

static void finishing_tag_loading_applies_the_new_filters_without_publishing_old_results(void)
{
    PersonalTags tags;
    begin_deferred_replacement(&tags);
    tags.result = STORE_DONE;
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
}

static void open_filters(void)
{
    explore();
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_FILTERS, discovery.mode);
}

static void artist_album_and_genre_filters_can_be_selected_and_cleared(void)
{
    for (int filter = 0; filter < 3; filter++) {
        start();
        open_filters();
        for (int step = 0; step < filter; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_PICKER, discovery.mode);
        CHECK(discovery.model.count(&discovery) > 1);
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_FILTERS, discovery.mode);
        uint16_t selected = filter == 0   ? discovery.query.artist
                            : filter == 1 ? discovery.query.album
                                          : discovery.query.genre;
        CHECK_EQUAL(0, selected);
        for (int step = 0; step < filter; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_PICKER, discovery.mode);
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        selected = filter == 0   ? discovery.query.artist
                   : filter == 1 ? discovery.query.album
                                 : discovery.query.genre;
        CHECK_EQUAL(LIBRARY_NONE, selected);
        key(KEY_BACK, KEY_GESTURE_PRESS);
        complete();
        CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
    }
}

static void year_and_duration_filters_apply_numeric_entries(void)
{
    for (int filter = 3; filter <= 6; filter++) {
        start();
        open_filters();
        for (int step = 0; step < filter; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_EDITOR, discovery.mode);
        for (int step = 0; step < 4; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        key(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS);
        key(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS);
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
        CHECK_EQUAL(DISCOVERY_FILTERS, discovery.mode);
        uint32_t actual = filter == 3   ? discovery.query.year_min
                          : filter == 4 ? discovery.query.year_max
                          : filter == 5 ? discovery.query.duration_min_ms
                                        : discovery.query.duration_max_ms;
        CHECK_EQUAL(filter <= 4 ? 42 : 42 * 60000, actual);
    }
}

static void every_sort_choice_and_direction_survives_a_rebuild(void)
{
    for (int sort = 0; sort <= 6; sort++) {
        start();
        explore();
        key(KEY_MENU, KEY_GESTURE_PRESS);
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_SORT, discovery.mode);
        for (int step = 0; step < sort; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        complete();
        CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
        if (sort < 6) {
            CHECK_EQUAL(sort, discovery.query.sort);
        } else {
            CHECK(discovery.query.descending);
        }
    }
}

static void finish_tag_change(void)
{
    for (int step = 0; step < 2000 && discovery.mode != DISCOVERY_TAGS; step++) {
        tick();
    }
    CHECK_EQUAL(DISCOVERY_TAGS, discovery.mode);
}

static void tag_creation_rename_and_deletion_update_the_list(void)
{
    start();
    discovery_screen_tags(&discovery, LIBRARY_NONE);
    tick();
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    finish_tag_change();
    CHECK_EQUAL(2, discovery.model.count(&discovery));
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(DISCOVERY_TAG_ACTIONS, discovery.mode);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_EDITOR, discovery.mode);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    finish_tag_change();
    CHECK(strstr(discovery.model.label(&discovery, 1), "b") != NULL);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(DISCOVERY_TAG_ACTIONS, discovery.mode);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_TAG_DELETE, discovery.mode);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    finish_tag_change();
    CHECK_EQUAL(1, discovery.model.count(&discovery));
}

static void personal_tag_filters_only_keep_assigned_tracks(void)
{
    start();
    discovery_screen_tags(&discovery, 0);
    tick();
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    finish_tag_change();
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    for (int step = 0; step < 2000; step++) {
        tick();
    }
    open_filters();
    for (int step = 0; step < 7; step++) {
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    }
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_TAG_FILTER, discovery.mode);
    CHECK_EQUAL(2, discovery.model.count(&discovery));
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(1, discovery.required_tags);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    complete();
    TrackOrderSource order = discovery_screen_order(&discovery);
    CHECK_EQUAL(1, order.count);
    uint16_t track = LIBRARY_NONE;
    CHECK(order.read(order.context, 0, 1, &track));
    CHECK_EQUAL(0, track);
}

static void result_actions_expose_the_selected_tracks_information(void)
{
    start();
    explore();
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(DISCOVERY_ACTIONS, discovery.mode);
    CHECK_EQUAL(4, discovery.model.count(&discovery));
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_INFO, discovery.mode);
    CHECK_EQUAL(11, discovery.model.count(&discovery));
    for (int row = 0; row < 11; row++) {
        discovery.list.top = row;
        tick();
        CHECK(discovery.model.label(&discovery, row) != NULL);
    }
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_RESULTS, discovery.mode);
}

static void track_actions_can_start_playback_and_return_to_results(void)
{
    for (int action = 2; action <= 3; action++) {
        start();
        Player player;
        player_init(&player, &fixture.library);
        discovery.config.player = &player;
        explore();
        uint16_t expected = LIBRARY_NONE;
        TrackOrderSource order = discovery_screen_order(&discovery);
        CHECK(order.read(order.context, 0, 1, &expected));
        key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
        CHECK_EQUAL(DISCOVERY_ACTIONS, discovery.mode);
        for (int step = 0; step < action; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_NOTICE, discovery.mode);
        CHECK(player_status(&player).has_track);
        CHECK_EQUAL(expected, player_status(&player).track);
        key(KEY_BACK, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_RESULTS, discovery.mode);
    }
}

static void out_of_range_year_is_rejected_without_changing_the_filter(void)
{
    start();
    open_filters();
    for (int step = 0; step < 3; step++) {
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    }
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_EDITOR, discovery.mode);
    key(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS);
    for (int digit = 0; digit < 6; digit++) {
        key(KEY_PLAY, KEY_GESTURE_PRESS);
    }
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    CHECK_EQUAL(DISCOVERY_NOTICE, discovery.mode);
    CHECK_EQUAL(0, discovery.query.year_min);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_FILTERS, discovery.mode);
}

static void a_tag_write_failure_shows_a_notice_without_creating_the_tag(void)
{
    start();
    discovery_screen_tags(&discovery, LIBRARY_NONE);
    tick();
    fake_hal_fail_music_writes(true);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
    for (int step = 0; step < 2000 && discovery.mode != DISCOVERY_NOTICE; step++) {
        tick();
    }
    CHECK_EQUAL(DISCOVERY_NOTICE, discovery.mode);
    fake_hal_fail_music_writes(false);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_TAGS, discovery.mode);
    CHECK_EQUAL(1, discovery.model.count(&discovery));
}

static void track_actions_enqueue_without_interrupting_current_playback(void)
{
    for (int action = 2; action <= 3; action++) {
        start();
        Player player;
        player_init(&player, &fixture.library);
        BrowseQuery single = {.kind = BROWSE_SINGLE_TRACK,
                              .group = LIBRARY_ARTISTS,
                              .subject = 0,
                              .artist = LIBRARY_NONE};
        CHECK(player_play_list(&player, single, 0, 7));
        discovery.config.player = &player;
        explore();
        key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        TrackOrderSource order = discovery_screen_order(&discovery);
        uint16_t expected = LIBRARY_NONE;
        CHECK(order.read(order.context, 1, 1, &expected));
        key(KEY_PLAY, KEY_GESTURE_LONG_PRESS);
        for (int step = 0; step < action; step++) {
            key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
        }
        key(KEY_PLAY, KEY_GESTURE_PRESS);
        CHECK_EQUAL(DISCOVERY_NOTICE, discovery.mode);
        CHECK_EQUAL(0, player_status(&player).track);
        CHECK_EQUAL(1, player.up_next.count);
        CHECK_EQUAL(expected, player.up_next.tracks[0]);
    }
}

static void draw_current_screen(void)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    discovery.screen.draw(&discovery, &canvas);
}

static void search_progress_cancel_failure_and_results_draw_distinct_states(void)
{
    static Color previous[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    start();
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    CHECK_EQUAL(LIBRARY_VIEW_SCANNING, discovery_screen_state(&discovery));
    draw_current_screen();
    memcpy(previous, pixels, sizeof previous);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    CHECK_EQUAL(LIBRARY_VIEW_CANCELLED, discovery_screen_state(&discovery));
    draw_current_screen();
    CHECK(memcmp(previous, pixels, sizeof previous) != 0);
    memcpy(previous, pixels, sizeof previous);
    refuse_writes = true;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_WRITE_FAILED, discovery_screen_state(&discovery));
    draw_current_screen();
    CHECK(memcmp(previous, pixels, sizeof previous) != 0);
    memcpy(previous, pixels, sizeof previous);
    refuse_writes = false;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
    draw_current_screen();
    CHECK(memcmp(previous, pixels, sizeof previous) != 0);
    memcpy(previous, pixels, sizeof previous);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    CHECK_EQUAL(DISCOVERY_REQUEST_PLAY, discovery.request);
    draw_current_screen();
    CHECK(memcmp(previous, pixels, sizeof previous) != 0);
}

static void active_filters_change_the_results_summary(void)
{
    static Color unfiltered[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    start();
    explore();
    draw_current_screen();
    memcpy(unfiltered, pixels, sizeof unfiltered);
    key(KEY_MENU, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_VOLUME_UP, KEY_GESTURE_PRESS);
    key(KEY_PLAY, KEY_GESTURE_PRESS);
    key(KEY_BACK, KEY_GESTURE_PRESS);
    complete();
    CHECK_EQUAL(0, discovery.query.artist);
    draw_current_screen();
    CHECK(memcmp(unfiltered, pixels, sizeof unfiltered) != 0);
}

static void every_active_constraint_is_visible_in_the_results_summary(void)
{
    static Color unfiltered[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    start();
    explore();
    draw_current_screen();
    memcpy(unfiltered, pixels, sizeof unfiltered);
    LibraryViewConfig original = discovery.query;
    for (int filter = 0; filter < 8; filter++) {
        discovery.query = original;
        discovery.required_tags = 0;
        switch (filter) {
        case 0:
            discovery.query.artist = 0;
            break;
        case 1:
            discovery.query.album = 0;
            break;
        case 2:
            discovery.query.genre = 0;
            break;
        case 3:
            discovery.query.year_min = 1;
            break;
        case 4:
            discovery.query.year_max = 2025;
            break;
        case 5:
            discovery.query.duration_min_ms = 60000;
            break;
        case 6:
            discovery.query.duration_max_ms = 120000;
            break;
        case 7:
            discovery.required_tags = 1;
            break;
        }
        draw_current_screen();
        CHECK(memcmp(unfiltered, pixels, sizeof unfiltered) != 0);
    }
    discovery.query = original;
    discovery.required_tags = 0;
    strcpy(discovery.query.text, "find");
    draw_current_screen();
    CHECK(memcmp(unfiltered, pixels, sizeof unfiltered) != 0);
}

static void a_search_with_no_matches_displays_an_empty_state(void)
{
    static Color results[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    start();
    explore();
    draw_current_screen();
    memcpy(results, pixels, sizeof results);
    discovery.query.year_min = UINT16_MAX;
    discovery_screen_open(&discovery, browse_root_query(BROWSE_ALL_SONGS), false);
    complete();
    CHECK_EQUAL(LIBRARY_VIEW_READY, discovery_screen_state(&discovery));
    CHECK_EQUAL(0, discovery_screen_order(&discovery).count);
    draw_current_screen();
    CHECK(memcmp(results, pixels, sizeof results) != 0);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(every_active_constraint_is_visible_in_the_results_summary),
        TEST_CASE(a_search_with_no_matches_displays_an_empty_state),
        TEST_CASE(search_progress_cancel_failure_and_results_draw_distinct_states),
        TEST_CASE(active_filters_change_the_results_summary),
        TEST_CASE(track_actions_enqueue_without_interrupting_current_playback),
        TEST_CASE(track_actions_can_start_playback_and_return_to_results),
        TEST_CASE(out_of_range_year_is_rejected_without_changing_the_filter),
        TEST_CASE(a_tag_write_failure_shows_a_notice_without_creating_the_tag),
        TEST_CASE(tag_creation_rename_and_deletion_update_the_list),
        TEST_CASE(personal_tag_filters_only_keep_assigned_tracks),
        TEST_CASE(result_actions_expose_the_selected_tracks_information),
        TEST_CASE(artist_album_and_genre_filters_can_be_selected_and_cleared),
        TEST_CASE(year_and_duration_filters_apply_numeric_entries),
        TEST_CASE(every_sort_choice_and_direction_survives_a_rebuild),
        TEST_CASE(backing_out_of_a_deferred_replacement_never_restores_stale_results),
        TEST_CASE(cancelling_a_deferred_replacement_never_exposes_the_previous_order),
        TEST_CASE(leaving_home_during_tag_loading_cannot_republish_an_old_order),
        TEST_CASE(a_cancelled_deferred_replacement_can_start_a_fresh_search),
        TEST_CASE(finishing_tag_loading_applies_the_new_filters_without_publishing_old_results),
        TEST_CASE(search_owns_menu_and_wake_without_changing_playback),
        TEST_CASE(playing_results_exposes_the_exact_transformed_order),
        TEST_CASE(selecting_a_sort_rebuilds_and_keeps_the_original_selected_track),
        TEST_CASE(filter_choices_change_the_query_and_reset_removes_every_constraint),
        TEST_CASE(cancellation_and_failed_reads_do_not_offer_a_playable_partial_list),
        TEST_CASE(options_back_and_background_cancellation_keep_a_completed_selection),
        TEST_CASE(deferred_tag_initialization_remains_a_pending_search),
        TEST_CASE(search_work_stops_at_the_clock_budget_and_services_audio),
        TEST_CASE(source_scope_is_combined_with_filters_and_survives_reset),
        TEST_CASE(personal_tag_filters_are_intersected_with_the_source_scope),
        TEST_CASE(a_view_save_failure_is_visible_without_disabling_results),
        TEST_CASE(metadata_rows_expose_fields_and_a_full_width_path),
        TEST_CASE(personal_tag_management_does_not_assign_a_nonexistent_track),
        TEST_CASE(reinitializing_the_same_screen_discards_the_previous_volume_work),
        TEST_CASE(cancelling_a_search_entry_returns_to_its_caller),
        TEST_CASE(text_entry_rejects_overflow_and_erases_whole_utf8_characters),
        TEST_CASE(native_layouts_keep_editor_hints_and_rows_inside_every_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
