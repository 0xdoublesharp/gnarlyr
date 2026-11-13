#include <stdbool.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screen_snapshot.h"
#include "screens/wire_boot.h"
#ifdef ECHO_TEST_GLYPHS
#include "sim_glyphs.h"
#endif

enum {
    THEMES = APPEARANCE_THEME_COUNT,
    FRAMES = 8,
    MARGIN = 6,
    PIXELS = HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT
};

static Color pixels[PIXELS];
static Color backdrop[PIXELS];

static const char *const MARK = "Wire";

static Theme theme_for(int appearance, int orientation)
{
    Theme theme = BOARD_THEME;
    default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    return theme;
}

static Rect screen_of(const Theme *theme)
{
    return (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height};
}

static Canvas draw(const Theme *theme, WireBootFrame frame)
{
    Canvas canvas = canvas_make(pixels, screen_of(theme));
    canvas_clear(&canvas, 0);
    wire_boot_draw(&canvas, theme, screen_of(theme), frame);
    return canvas;
}

static Canvas plain_backdrop(const Theme *theme)
{
    Canvas canvas = canvas_make(backdrop, screen_of(theme));
    canvas_fill_gradient(&canvas, screen_of(theme), theme->colors.backdrop_top,
                         theme->colors.backdrop_bottom);
    return canvas;
}

static int horizon_of(const Theme *theme)
{
    return theme->metrics.screen_height * 9 / 16;
}

static int differing_pixels(Rect area, const Theme *theme)
{
    Canvas drawn = canvas_make(pixels, screen_of(theme));
    Canvas plain = canvas_make(backdrop, screen_of(theme));
    int count = 0;
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            count += canvas_pixel_at(&drawn, x, y) != canvas_pixel_at(&plain, x, y);
        }
    }
    return count;
}

static Rect below_horizon(const Theme *theme)
{
    int top = horizon_of(theme) + 2;
    return (Rect){0, top, theme->metrics.screen_width, theme->metrics.screen_height - top};
}

static Rect above_horizon(const Theme *theme)
{
    return (Rect){0, 0, theme->metrics.screen_width, horizon_of(theme) - 1};
}

static void the_opening_starts_as_the_plain_backdrop(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        Theme theme = theme_for(appearance, ORIENTATION_LANDSCAPE);
        draw(&theme, (WireBootFrame){MARK, 0, false});
        plain_backdrop(&theme);
        CHECK_EQUAL(0, differing_pixels(screen_of(&theme), &theme));
    }
}

static void the_opening_ends_with_the_grid_below_and_the_mark_above_the_horizon(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            Theme theme = theme_for(appearance, orientation);
            draw(&theme, (WireBootFrame){MARK, WIRE_BOOT_ONE, false});
            plain_backdrop(&theme);
            CHECK(differing_pixels(below_horizon(&theme), &theme) > 0);
            CHECK(differing_pixels(above_horizon(&theme), &theme) > 0);
            Canvas canvas = canvas_make(pixels, screen_of(&theme));
            int middle = theme.metrics.screen_width / 2;
            CHECK_EQUAL(theme.colors.accent, canvas_pixel_at(&canvas, middle, horizon_of(&theme)));
        }
    }
}

static void draw_without_mark(const Theme *theme, uint16_t progress)
{
    Canvas canvas = canvas_make(backdrop, screen_of(theme));
    canvas_clear(&canvas, 0);
    wire_boot_draw(&canvas, theme, screen_of(theme), (WireBootFrame){"", progress, false});
}

static void the_mark_rises_from_behind_the_horizon(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = theme_for(APPEARANCE_NEON_NOIR, orientation);
        int shown = 0;
        for (int frame = 0; frame <= FRAMES; frame++) {
            uint16_t progress = (uint16_t)(frame * WIRE_BOOT_ONE / FRAMES);
            draw_without_mark(&theme, progress);
            draw(&theme, (WireBootFrame){MARK, progress, false});
            CHECK_EQUAL(0, differing_pixels(below_horizon(&theme), &theme));
            shown = differing_pixels(above_horizon(&theme), &theme);
            CHECK(frame > 0 || shown == 0);
        }
        CHECK(shown > 0);
    }
}

static void the_closing_folds_the_grid_into_the_horizon_line(void)
{
    Theme theme = theme_for(APPEARANCE_SUNSET, ORIENTATION_LANDSCAPE);
    draw(&theme, (WireBootFrame){MARK, 0, true});
    plain_backdrop(&theme);
    CHECK(differing_pixels(below_horizon(&theme), &theme) > 0);
    draw(&theme, (WireBootFrame){MARK, WIRE_BOOT_ONE * 5 / 8, true});
    Canvas canvas = canvas_make(pixels, screen_of(&theme));
    Rect floor = below_horizon(&theme);
    for (int y = floor.y; y < floor.y + floor.height; y++) {
        CHECK_EQUAL(canvas_pixel_at(&canvas, 0, y),
                    canvas_pixel_at(&canvas, theme.metrics.screen_width / 3, y));
    }
}

static void the_closing_ends_dark(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        Theme theme = theme_for(appearance, ORIENTATION_PORTRAIT);
        Canvas canvas = draw(&theme, (WireBootFrame){MARK, WIRE_BOOT_ONE, true});
        Rect screen = screen_of(&theme);
        for (int y = 0; y < screen.height; y++) {
            for (int x = 0; x < screen.width; x++) {
                CHECK_EQUAL(0, canvas_pixel_at(&canvas, x, y));
            }
        }
    }
}

static void every_frame_stays_inside_its_area(void)
{
    Theme theme = theme_for(APPEARANCE_CLASSIC, ORIENTATION_LANDSCAPE);
    Rect screen = screen_of(&theme);
    Rect area = {MARGIN, MARGIN, screen.width - 2 * MARGIN, screen.height - 2 * MARGIN};
    Canvas canvas = canvas_make(pixels, screen);
    for (int frame = 0; frame <= FRAMES; frame++) {
        for (int closing = 0; closing < 2; closing++) {
            canvas_clear(&canvas, 1);
            WireBootFrame shown = {MARK, (uint16_t)(frame * WIRE_BOOT_ONE / FRAMES), closing != 0};
            wire_boot_draw(&canvas, &theme, area, shown);
            for (int x = 0; x < screen.width; x++) {
                CHECK_EQUAL(1, canvas_pixel_at(&canvas, x, MARGIN - 1));
                CHECK_EQUAL(1, canvas_pixel_at(&canvas, x, screen.height - MARGIN));
            }
            for (int y = 0; y < screen.height; y++) {
                CHECK_EQUAL(1, canvas_pixel_at(&canvas, MARGIN - 1, y));
                CHECK_EQUAL(1, canvas_pixel_at(&canvas, screen.width - MARGIN, y));
            }
        }
    }
}

static void same_pixels(const Theme *theme)
{
    Rect screen = screen_of(theme);
    CHECK_EQUAL(0,
                memcmp(pixels, backdrop, sizeof(Color) * (size_t)(screen.width * screen.height)));
}

static void the_fade_follows_the_clock_and_its_direction(void)
{
    Theme theme = theme_for(APPEARANCE_SEA_GLASS, ORIENTATION_LANDSCAPE);
    static const FadeTiming TIMINGS[] = {{WIRE_BOOT_OPEN_MS, 0, true},
                                         {WIRE_BOOT_CLOSE_MS, 0, false}};
    for (int closing = 0; closing < 2; closing++) {
        Fade fade;
        fake_hal_set_milliseconds(1000);
        fade_start(&fade, 1000, TIMINGS[closing]);
        uint32_t steps[] = {0, TIMINGS[closing].fade_ms / 2, TIMINGS[closing].fade_ms * 2};
        uint16_t expected[] = {0, WIRE_BOOT_ONE / 2, WIRE_BOOT_ONE};
        for (int step = 0; step < 3; step++) {
            fake_hal_set_milliseconds(1000 + steps[step]);
            Canvas canvas = canvas_make(backdrop, screen_of(&theme));
            wire_boot_draw(&canvas, &theme, screen_of(&theme),
                           (WireBootFrame){MARK, expected[step], closing != 0});
            canvas = canvas_make(pixels, screen_of(&theme));
            wire_boot_fade(&canvas, &theme, &fade, MARK);
            same_pixels(&theme);
        }
    }
}

static void every_theme_and_orientation_draws_both_sequences(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            Theme theme = theme_for(appearance, orientation);
            int shot = appearance * 1000 + orientation * 100;
            for (int frame = 0; frame <= FRAMES; frame++) {
                uint16_t progress = (uint16_t)(frame * WIRE_BOOT_ONE / FRAMES);
                Canvas canvas = draw(&theme, (WireBootFrame){FIRMWARE_NAME, progress, false});
                screen_snapshot_write(&canvas, "wire-boot-open", shot + frame);
                canvas = draw(&theme, (WireBootFrame){i18n_text(STR_GOODBYE), progress, true});
                screen_snapshot_write(&canvas, "wire-boot-close", shot + frame);
            }
        }
    }
}

int main(void)
{
#ifdef ECHO_TEST_GLYPHS
    sim_glyphs_attach();
#endif
    static const TestCase cases[] = {
        TEST_CASE(the_opening_starts_as_the_plain_backdrop),
        TEST_CASE(the_opening_ends_with_the_grid_below_and_the_mark_above_the_horizon),
        TEST_CASE(the_mark_rises_from_behind_the_horizon),
        TEST_CASE(the_closing_folds_the_grid_into_the_horizon_line),
        TEST_CASE(the_closing_ends_dark),
        TEST_CASE(every_frame_stays_inside_its_area),
        TEST_CASE(the_fade_follows_the_clock_and_its_direction),
        TEST_CASE(every_theme_and_orientation_draws_both_sequences),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
