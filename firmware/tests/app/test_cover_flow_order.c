#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "big_library.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "screens/cover_flow_screen.h"

enum {
    BIG_ALBUMS = 2500,
    GROUP_FIRST_LINK = 4,
    GROUP_LINK_COUNT = 8
};

static FixtureLibrary fixture;
static BigLibrary big;
static uint8_t seen[BIG_ALBUMS];
static CoverFlowScreen flow;
static CoverCache covers;
static FavoritesStore favorites;
static BrowsePool browse;
static Player player;
static Toast toast;
static Screen *screen;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static void start_flow(Library *library)
{
    fake_hal_reset();
    player_init(&player, library);
    favorites_store_load(&favorites, library);
    cover_cache_open(&covers, library);
    toast = (Toast){0};
    browse_pool_init(&browse, (BrowsePoolConfig){.library = library,
                                                 .theme = &BOARD_THEME,
                                                 .player = &player,
                                                 .favorites = &favorites,
                                                 .toast = &toast});
    screen = cover_flow_screen_init(&flow, (CoverFlowConfig){.library = library,
                                                             .theme = &BOARD_THEME,
                                                             .covers = &covers,
                                                             .favorites = &favorites,
                                                             .toast = &toast,
                                                             .browse = &browse,
                                                             .player = &player});
    screen->enter(screen->context, 0);
}

static void volume(Key key, KeyGesture gesture, uint32_t now_ms)
{
    screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = gesture}, now_ms);
}

static int album_at(const Library *library, CoverFlowOrder order, uint32_t position)
{
    uint16_t album = 0;
    return cover_flow_order_album(library, order, position, &album) ? album : -1;
}

static void albums_follow_their_album_artists(void)
{
    Library *library = fixture_library_open(&fixture);
    CoverFlowOrder order = cover_flow_order_make(library);
    CHECK_EQUAL(FIXTURE_ALBUMS, order.count);
    uint32_t position = 0;
    for (uint32_t index = 0; index < library_count(library, LIBRARY_ALBUM_ARTISTS); index++) {
        LibraryGroup group;
        CHECK(library_read_group(library, LIBRARY_ALBUM_ARTISTS, index, &group));
        for (uint32_t link = 0; link < group.link_count; link++) {
            uint16_t album = 0;
            CHECK(library_read_index(library, LIBRARY_LINKS, group.first_link + link, &album));
            CHECK_EQUAL(album, album_at(library, order, position++));
        }
    }
    CHECK_EQUAL(-1, album_at(library, order, position));
}

static void each_album_is_found_where_it_is_shown(void)
{
    Library *library = fixture_library_open(&fixture);
    CoverFlowOrder order = cover_flow_order_make(library);
    for (uint32_t position = 0; position < order.count; position++) {
        uint32_t found = 0;
        CHECK(cover_flow_order_find(library, order, (uint16_t)album_at(library, order, position),
                                    &found));
        CHECK_EQUAL(position, found);
    }
    uint32_t found = 0;
    CHECK(!cover_flow_order_find(library, order, FIXTURE_ALBUMS, &found));
}

static void every_album_of_a_big_library_is_shown_once(void)
{
    Library *library = big_library_open(&big, BIG_ALBUMS * 2, 2);
    CoverFlowOrder order = cover_flow_order_make(library);
    CHECK_EQUAL(BIG_ALBUMS, order.count);
    for (uint32_t position = 0; position < order.count; position++) {
        int album = album_at(library, order, position);
        CHECK(album >= 0 && album < BIG_ALBUMS && seen[album] == 0);
        seen[album] = 1;
        uint32_t found = 0;
        CHECK(cover_flow_order_find(library, order, (uint16_t)album, &found));
        CHECK_EQUAL(position, found);
    }
    big_library_close(&big);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (int shift = 0; shift < 4; shift++) {
        bytes[shift] = (uint8_t)(value >> (shift * 8));
    }
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void swap_first_two_album_artists(Library *library)
{
    LibrarySectionInfo info = library->sections[LIBRARY_ALBUM_ARTISTS];
    uint8_t *first = fixture.bytes + info.offset;
    uint8_t *second = first + info.record_size;
    uint32_t first_link = get_u32(first + GROUP_FIRST_LINK);
    uint32_t first_count = first[GROUP_LINK_COUNT] | (uint32_t)first[GROUP_LINK_COUNT + 1] << 8;
    uint32_t second_count = second[GROUP_LINK_COUNT] | (uint32_t)second[GROUP_LINK_COUNT + 1] << 8;
    put_u32(first + GROUP_FIRST_LINK, first_link + second_count);
    put_u32(second + GROUP_FIRST_LINK, first_link);
    first[GROUP_LINK_COUNT] = (uint8_t)second_count;
    second[GROUP_LINK_COUNT] = (uint8_t)first_count;
}

static void out_of_order_links_fall_back_to_album_order(void)
{
    Library *library = fixture_library_open(&fixture);
    swap_first_two_album_artists(library);
    CoverFlowOrder order = cover_flow_order_make(library);
    CHECK(!order.linked);
    CHECK_EQUAL(FIXTURE_ALBUMS, order.count);
    for (uint32_t position = 0; position < FIXTURE_ALBUMS; position++) {
        CHECK_EQUAL(position, album_at(library, order, position));
        uint32_t found = 0;
        CHECK(cover_flow_order_find(library, order, (uint16_t)position, &found));
        CHECK_EQUAL(position, found);
    }
}

static void holding_volume_accelerates_across_albums(void)
{
    start_flow(fixture_library_open(&fixture));
    CHECK_EQUAL(1, flow.target);
    volume(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 100);
    CHECK_EQUAL(2, flow.target);
    volume(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 100 + LIST_FAST_SCROLL_AFTER_MS);
    CHECK_EQUAL(4, flow.target);
}

static void a_long_hold_jumps_to_the_next_artist_letter(void)
{
    start_flow(fixture_library_open(&fixture));
    volume(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 100);
    volume(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 200);
    CHECK_EQUAL(1, flow.target);
    volume(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 200 + LIST_SECTION_JUMP_AFTER_MS);
    CHECK_EQUAL(4, flow.target);
}

static void backward_holds_return_to_the_start_of_the_previous_artist_letter(void)
{
    start_flow(fixture_library_open(&fixture));
    flow.target = 5;
    volume(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 100);
    CHECK_EQUAL(4, flow.target);
    volume(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 100 + LIST_SECTION_JUMP_AFTER_MS);
    CHECK_EQUAL(1, flow.target);
    volume(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 200 + LIST_SECTION_JUMP_AFTER_MS);
    CHECK_EQUAL(0, flow.target);
}

static void taps_and_holds_keep_the_existing_clamped_endpoints(void)
{
    start_flow(fixture_library_open(&fixture));
    for (int index = 0; index < 10; index++) {
        volume(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 100);
    }
    CHECK_EQUAL(0, flow.target);
    volume(KEY_VOLUME_DOWN, KEY_GESTURE_REPEAT, 100 + LIST_SECTION_JUMP_AFTER_MS);
    CHECK_EQUAL(0, flow.target);
    for (int index = 0; index < 10; index++) {
        volume(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 4000);
    }
    CHECK_EQUAL(5, flow.target);
    volume(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 4000 + LIST_SECTION_JUMP_AFTER_MS);
    CHECK_EQUAL(5, flow.target);
}

static void fallback_order_jumps_by_album_title_instead_of_unsorted_artists(void)
{
    Library *library = fixture_library_open(&fixture);
    swap_first_two_album_artists(library);
    start_flow(library);
    CHECK(!flow.order.linked);
    volume(KEY_VOLUME_DOWN, KEY_GESTURE_PRESS, 100);
    volume(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 200);
    volume(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 200 + LIST_SECTION_JUMP_AFTER_MS);
    CHECK_EQUAL(3, flow.target);
}

static void a_new_cover_flow_explains_holding_volume_once(void)
{
    start_flow(fixture_library_open(&fixture));
    CHECK(toast.visible);
    CHECK(toast.text != NULL && strcmp(toast.text, "Hold volume: A-Z") == 0);
    toast.visible = false;
    screen->enter(screen->context, 2000);
    CHECK(!toast.visible);
}

static void artwork_loads_in_the_direction_of_travel_before_the_opposite_side(void)
{
    start_flow(fixture_library_open(&fixture));
    uint16_t album = 0;
    CHECK(cover_flow_order_album(&fixture.library, flow.order, 2, &album));
    CHECK(cover_cache_load_half(&covers, 3, album));
    flow.position = 3 * COVER_FLOW_ONE;
    flow.target = 4;
    screen->update(screen->context, 0);
    CHECK_EQUAL(4, covers.halves[4 % COVER_CACHE_SLOTS].position);
    CHECK(covers.halves[4 % COVER_CACHE_SLOTS].state != COVER_EMPTY);
    CHECK_EQUAL(-1, covers.halves[2 % COVER_CACHE_SLOTS].position);
}

static void hold_timing_survives_the_millisecond_clock_wrapping(void)
{
    start_flow(fixture_library_open(&fixture));
    volume(KEY_VOLUME_UP, KEY_GESTURE_PRESS, UINT32_MAX - 1000);
    volume(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 500);
    CHECK_EQUAL(4, flow.target);
}

static void the_letter_badge_is_visible_then_expires_in_every_screen_layout(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            start_flow(fixture_library_open(&fixture));
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            flow.config.theme = &theme;
            volume(KEY_VOLUME_UP, KEY_GESTURE_PRESS, 100);
            volume(KEY_VOLUME_UP, KEY_GESTURE_REPEAT, 100 + LIST_FAST_SCROLL_AFTER_MS);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            screen->draw(screen->context, &canvas);
            CHECK_EQUAL(theme.colors.selection_background,
                        canvas_pixel_at(&canvas, theme.metrics.padding, theme.metrics.padding));
            screen->update(screen->context,
                           100 + LIST_FAST_SCROLL_AFTER_MS + LIST_SECTION_SHOWN_MS);
            screen->draw(screen->context, &canvas);
            CHECK(canvas_pixel_at(&canvas, theme.metrics.padding, theme.metrics.padding) !=
                  theme.colors.selection_background);
            Rect hint = toast_area(&toast, &theme);
            CHECK(hint.x >= 0 && hint.y >= 0);
            CHECK(hint.x + hint.width <= theme.metrics.screen_width);
            CHECK(hint.y + hint.height <= theme.metrics.screen_height);
        }
    }
}

static void preparing_cover_flow_does_not_walk_the_library_before_it_opens(void)
{
    Library *library = fixture_library_open(&fixture);
    int before = fixture.memory.reads;
    cover_flow_screen_init(&flow, (CoverFlowConfig){.library = library, .theme = &BOARD_THEME});
    CHECK_EQUAL(before, fixture.memory.reads);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(preparing_cover_flow_does_not_walk_the_library_before_it_opens),
        TEST_CASE(albums_follow_their_album_artists),
        TEST_CASE(each_album_is_found_where_it_is_shown),
        TEST_CASE(every_album_of_a_big_library_is_shown_once),
        TEST_CASE(out_of_order_links_fall_back_to_album_order),
        TEST_CASE(holding_volume_accelerates_across_albums),
        TEST_CASE(a_long_hold_jumps_to_the_next_artist_letter),
        TEST_CASE(backward_holds_return_to_the_start_of_the_previous_artist_letter),
        TEST_CASE(taps_and_holds_keep_the_existing_clamped_endpoints),
        TEST_CASE(fallback_order_jumps_by_album_title_instead_of_unsorted_artists),
        TEST_CASE(a_new_cover_flow_explains_holding_volume_once),
        TEST_CASE(artwork_loads_in_the_direction_of_travel_before_the_opposite_side),
        TEST_CASE(hold_timing_survives_the_millisecond_clock_wrapping),
        TEST_CASE(the_letter_badge_is_visible_then_expires_in_every_screen_layout),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
