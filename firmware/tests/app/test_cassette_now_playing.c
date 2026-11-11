#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "screens/cassette_now_playing.h"
#include "text_spy.h"

static void tape_area_moves_between_empty_and_full_without_overflow(void)
{
    CHECK_EQUAL(8, cassette_now_playing_radius(0, 100, 24));
    CHECK_EQUAL(24, cassette_now_playing_radius(100, 100, 24));
    CHECK_EQUAL(24, cassette_now_playing_radius(UINT32_MAX, 100, 24));
    CHECK_EQUAL(8, cassette_now_playing_radius(10, 0, 24));
    int previous = 8;
    for (uint32_t position = 0; position <= 100; position++) {
        int radius = cassette_now_playing_radius(position, 100, 24);
        CHECK(radius >= previous && radius <= 24);
        previous = radius;
    }
    CHECK_EQUAL(cassette_now_playing_radius(50, 100, 24),
                cassette_now_playing_radius(UINT32_MAX / 2, UINT32_MAX, 24));
}

static void labels_and_reels_fit_all_sizes_and_orientations(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            CassetteNowPlayingLayout layout = cassette_now_playing_layout(&theme);
            CHECK(layout.body.width > 160);
            for (int line = 0; line < 3; line++) {
                CHECK(layout.label[line].width > 100);
                CHECK(layout.label[line].y >= layout.body.y);
                CHECK(layout.label[line].y + layout.label[line].height <= layout.reels[0].y);
            }
            CHECK(rect_is_empty(rect_intersection(layout.reels[0], layout.reels[1])));
        }
    }
}

static Color pixels[480 * 480];
static Color before[480 * 480];
static NowPlayingScreen screen;
static Theme theme;
static Settings settings;

static Canvas start_view(int orientation, int size)
{
    fake_hal_reset();
    theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    settings = settings_defaults();
    settings_set_now_playing_style(&settings, NOW_PLAYING_CASSETTE);
    now_playing_screen_init(&screen,
                            (NowPlayingConfig){.theme = &theme,
                                               .settings = &settings,
                                               .cassette_draw = cassette_now_playing_draw});
    screen.status =
        (PlayerStatus){.has_track = true, .state = PLAYER_PLAYING, .duration_ms = 20000};
    strcpy(screen.title, "Night Transmission / Midnight on the endless violet horizon");
    strcpy(screen.artist, "The Midnight Transmission Orchestra / live in the clouds");
    strcpy(screen.album, "Parallel Dreams / the complete extended cassette collection");
    return canvas_make(pixels,
                       (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
}

static bool inside(Rect area, int x, int y)
{
    return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
}

static void reel_frames_change_only_the_two_reel_rectangles(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Canvas canvas = start_view(orientation, TEXT_SIZE_LARGE);
        screen.screen.draw(&screen, &canvas);
        memcpy(before, pixels, sizeof pixels);
        screen.reel_position_ms = 10000;
        screen.screen.draw(&screen, &canvas);
        CHECK(memcmp(before, pixels, sizeof pixels) != 0);
        CassetteNowPlayingLayout layout = cassette_now_playing_layout(&theme);
        for (int y = 0; y < canvas.bounds.height; y++) {
            for (int x = 0; x < canvas.bounds.width; x++) {
                if (!inside(layout.reels[0], x, y) && !inside(layout.reels[1], x, y)) {
                    CHECK_EQUAL(before[y * canvas.bounds.width + x],
                                pixels[y * canvas.bounds.width + x]);
                }
            }
        }
    }
}

static void long_labels_truncate_at_a_complete_character_with_an_ellipsis(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Canvas canvas = start_view(orientation, size);
            text_spy_reset();
            cassette_now_playing_draw(&screen, &canvas);
            int truncated = 0;
            for (int index = 0; index < text_spy_count(); index++) {
                const TextSpyEntry *entry = text_spy_entry(index);
                if (strcmp(entry->rest, screen.title) == 0 ||
                    strcmp(entry->rest, screen.artist) == 0 ||
                    strcmp(entry->rest, screen.album) == 0) {
                    CHECK(strlen(entry->shown) < strlen(entry->rest));
                    CHECK(((unsigned char)entry->rest[strlen(entry->shown)] & 0xC0) != 0x80);
                    truncated++;
                }
            }
            CHECK_EQUAL(3, truncated);
        }
    }
}

static void motion_is_capped_and_stops_when_paused_hidden_or_seeking(void)
{
    start_view(0, TEXT_SIZE_MEDIUM);
    uint32_t hashes[480];
    FrameDiff diff;
    frame_diff_init(&diff, hashes, 480);
    cassette_now_playing_animate(&screen, &diff, false);
    CHECK_EQUAL(0, fake_hal_pixels_written());
    for (uint32_t now = 0; now < 1000; now += 4) {
        screen.now_ms = now;
        screen.status.position_ms = now;
        cassette_now_playing_animate(&screen, &diff, true);
    }
    CassetteNowPlayingLayout layout = cassette_now_playing_layout(&theme);
    long per_frame = 2 * layout.reels[0].width * layout.reels[0].height;
    CHECK_EQUAL(13 * per_frame, fake_hal_pixels_written());
    long written = fake_hal_pixels_written();
    screen.status.state = PLAYER_PAUSED;
    screen.now_ms += 1000;
    cassette_now_playing_animate(&screen, &diff, true);
    CHECK(rect_is_empty(diff.masked));
    screen.status.state = PLAYER_PLAYING;
    cassette_now_playing_animate(&screen, &diff, false);
    screen.scrubbing = true;
    cassette_now_playing_animate(&screen, &diff, true);
    CHECK_EQUAL(written, fake_hal_pixels_written());
}

static void paused_reels_ignore_elapsed_wall_time(void)
{
    Canvas canvas = start_view(0, TEXT_SIZE_LARGE);
    screen.status.state = PLAYER_PAUSED;
    screen.status.position_ms = 12000;
    screen.screen.draw(&screen, &canvas);
    memcpy(before, pixels, sizeof pixels);
    screen.now_ms = 95000;
    screen.screen.draw(&screen, &canvas);
    CHECK(memcmp(before, pixels, sizeof pixels) == 0);
}

static void classic_footer_uses_the_shells_contrasting_header_ink(void)
{
    Canvas canvas = start_view(0, TEXT_SIZE_MEDIUM);
    default_theme_set_appearance(&theme, APPEARANCE_CLASSIC);
    cassette_now_playing_draw(&screen, &canvas);
    Rect body = cassette_now_playing_layout(&theme).body;
    int ink = 0;
    for (int y = body.y + body.height - 20; y < body.y + body.height - 2; y++) {
        for (int x = body.x + 12; x < body.x + 52; x++) {
            ink += canvas_pixel_at(&canvas, x, y) == theme.colors.header_text ? 1 : 0;
        }
    }
    CHECK(ink > 10);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(classic_footer_uses_the_shells_contrasting_header_ink),
        TEST_CASE(reel_frames_change_only_the_two_reel_rectangles),
        TEST_CASE(long_labels_truncate_at_a_complete_character_with_an_ellipsis),
        TEST_CASE(motion_is_capped_and_stops_when_paused_hidden_or_seeking),
        TEST_CASE(paused_reels_ignore_elapsed_wall_time),
        TEST_CASE(tape_area_moves_between_empty_and_full_without_overflow),
        TEST_CASE(labels_and_reels_fit_all_sizes_and_orientations),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
