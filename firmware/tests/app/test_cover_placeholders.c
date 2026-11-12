#include <stdbool.h>
#include <stdint.h>

#include "board_layout.h"
#include "check.h"
#include "cover_placeholders.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "fonts/fonts.h"
#include "screens/cover_flow_screen.h"
#include "ui/placeholder_art.h"

enum {
    RED_ALBUM = 0,
    BARE_ALBUM = 1,
    RED = 0xF800,
    SETTLE_MS = 2000,
    FRAME_MS = 20,
    MIN_CONTRAST = 112
};

static FixtureLibrary fixture;
static CoverCache cache;
static CoverFlowScreen flow;
static FavoritesStore favorites;
static BrowsePool browse;
static Player player;
static Toast toast;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static Library *open_cache(bool with_covers)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_covers_bytes(&size);
    fake_hal_reset();
    if (with_covers) {
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/covers.bin", bytes, size);
    }
    Library *library = fixture_library_open(&fixture);
    cover_cache_open(&cache, library);
    return library;
}

static Theme theme_in(AppearanceTheme appearance)
{
    Theme theme = BOARD_THEME;
    default_theme_invalidate(&theme);
    default_theme_set_appearance(&theme, appearance);
    return theme;
}

static Color expected_top(const Theme *theme, uint16_t album)
{
    return placeholder_art_ink(&theme->colors, library_album_key(cache.library, album)).top;
}

static void an_album_without_art_gets_a_placeholder_tile(void)
{
    open_cache(true);
    Theme theme = theme_in(APPEARANCE_GRAPHITE);
    Image image;
    cover_cache_load_half(&cache, 1, BARE_ALBUM);
    cover_placeholders_paint(&cache, &theme, COVER_MISSING);
    CHECK_EQUAL(COVER_PLACEHOLDER, cover_cache_half(&cache, 1, BARE_ALBUM, &image));
    CHECK_EQUAL(COVER_HALF_SIZE, image.width);
    CHECK_EQUAL(expected_top(&theme, BARE_ALBUM), image.pixels[0]);
    CHECK(!cover_cache_load_half(&cache, 1, BARE_ALBUM));
}

static void albums_with_art_keep_their_cover(void)
{
    open_cache(true);
    Theme theme = theme_in(APPEARANCE_GRAPHITE);
    Image image;
    cover_cache_load_full(&cache, RED_ALBUM);
    cover_placeholders_paint(&cache, &theme, COVER_MISSING);
    CHECK_EQUAL(COVER_LOADED, cover_cache_full(&cache, RED_ALBUM, &image));
    CHECK_EQUAL(RED, image.pixels[0]);
}

static void without_a_covers_file_every_album_gets_a_placeholder(void)
{
    open_cache(false);
    Theme theme = theme_in(APPEARANCE_CLASSIC);
    Image image;
    cover_cache_load_full(&cache, RED_ALBUM);
    cover_placeholders_paint(&cache, &theme, COVER_MISSING);
    CHECK_EQUAL(COVER_PLACEHOLDER, cover_cache_full(&cache, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_FULL_SIZE, image.width);
    CHECK_EQUAL(expected_top(&theme, RED_ALBUM), image.pixels[0]);
}

static void a_new_theme_repaints_placeholders_in_place(void)
{
    open_cache(false);
    Theme graphite = theme_in(APPEARANCE_GRAPHITE);
    Theme sunset = theme_in(APPEARANCE_SUNSET);
    Image image;
    cover_cache_load_half(&cache, 2, BARE_ALBUM);
    cover_placeholders_paint(&cache, &graphite, COVER_MISSING);
    cover_placeholders_paint(&cache, &sunset, COVER_PLACEHOLDER);
    CHECK_EQUAL(COVER_PLACEHOLDER, cover_cache_half(&cache, 2, BARE_ALBUM, &image));
    CHECK_EQUAL(expected_top(&sunset, BARE_ALBUM), image.pixels[0]);
}

static void placeholders_never_count_as_real_artwork_elsewhere(void)
{
    open_cache(false);
    Theme theme = theme_in(APPEARANCE_GRAPHITE);
    Image image = {0};
    cover_cache_load_full(&cache, BARE_ALBUM);
    cover_placeholders_paint(&cache, &theme, COVER_MISSING);
    CHECK(cover_cache_full(&cache, BARE_ALBUM, &image) != COVER_LOADED);
    CHECK(!cover_cache_load_full(&cache, BARE_ALBUM));
}

static int luma(Color color)
{
    return ((color >> 11) * 8 * 77 + ((color >> 5) & 0x3F) * 4 * 150 + (color & 0x1F) * 8 * 29) >>
           8;
}

static void placeholder_text_is_readable_in_every_theme(void)
{
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        Theme theme = theme_in((AppearanceTheme)appearance);
        for (uint32_t seed = 0; seed < 64; seed++) {
            ArtInk ink = placeholder_art_ink(&theme.colors, seed * 2654435761u);
            int difference = luma(ink.top) - luma(ink.text);
            CHECK((difference < 0 ? -difference : difference) >= MIN_CONTRAST);
            CHECK_EQUAL(theme.colors.text, ink.text);
            CHECK(ink.top != theme.colors.background);
        }
    }
}

static Screen *open_flow(Library *library, const Theme *theme)
{
    player_init(&player, library);
    favorites_store_load(&favorites, library);
    toast = (Toast){0};
    browse_pool_init(&browse, (BrowsePoolConfig){.library = library,
                                                 .theme = theme,
                                                 .player = &player,
                                                 .favorites = &favorites,
                                                 .toast = &toast});
    return cover_flow_screen_init(&flow, (CoverFlowConfig){.library = library,
                                                           .theme = theme,
                                                           .covers = &cache,
                                                           .favorites = &favorites,
                                                           .toast = &toast,
                                                           .browse = &browse,
                                                           .player = &player});
}

static void cover_flow_shows_a_placeholder_for_an_album_without_art(void)
{
    Library *library = open_cache(true);
    Theme theme = theme_in(APPEARANCE_SEA_GLASS);
    Screen *screen = open_flow(library, &theme);
    screen->enter(screen->context, 0);
    uint32_t position = 0;
    CHECK(cover_flow_order_find(library, flow.order, BARE_ALBUM, &position));
    flow.positioned = true;
    flow.target = (int)position + 1;
    screen->enter(screen->context, 0);
    for (uint32_t now_ms = FRAME_MS; now_ms <= SETTLE_MS; now_ms += FRAME_MS) {
        screen->update(screen->context, now_ms);
    }
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    screen->draw(screen->context, &canvas);
    Image image;
    CHECK_EQUAL(COVER_PLACEHOLDER, cover_cache_full(&cache, BARE_ALBUM, &image));
    Quad quad = cover_flow_quad(&BOARD_LAYOUT.cover_flow, 0);
    Color top = canvas_pixel_at(&canvas, quad.left + quad.width / 2, quad.left_top);
    CHECK_EQUAL(expected_top(&theme, BARE_ALBUM), top);
}

static void cover_flow_repaints_placeholders_after_a_theme_change(void)
{
    Library *library = open_cache(false);
    Theme theme = theme_in(APPEARANCE_GRAPHITE);
    Screen *screen = open_flow(library, &theme);
    screen->enter(screen->context, 0);
    for (uint32_t now_ms = FRAME_MS; now_ms <= SETTLE_MS; now_ms += FRAME_MS) {
        screen->update(screen->context, now_ms);
    }
    default_theme_set_appearance(&theme, APPEARANCE_NEON_NOIR);
    screen->enter(screen->context, SETTLE_MS);
    for (int slot = 0; slot < COVER_CACHE_SLOTS; slot++) {
        const CoverSlot *half = &cache.halves[slot];
        if (half->position >= 0) {
            CHECK_EQUAL(COVER_PLACEHOLDER, half->state);
            CHECK_EQUAL(expected_top(&theme, half->album), half->pixels[0]);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_album_without_art_gets_a_placeholder_tile),
        TEST_CASE(albums_with_art_keep_their_cover),
        TEST_CASE(without_a_covers_file_every_album_gets_a_placeholder),
        TEST_CASE(a_new_theme_repaints_placeholders_in_place),
        TEST_CASE(placeholders_never_count_as_real_artwork_elsewhere),
        TEST_CASE(placeholder_text_is_readable_in_every_theme),
        TEST_CASE(cover_flow_shows_a_placeholder_for_an_album_without_art),
        TEST_CASE(cover_flow_repaints_placeholders_after_a_theme_change),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
