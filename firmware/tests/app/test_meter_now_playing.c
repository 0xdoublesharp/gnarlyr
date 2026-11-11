#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screens/meter_now_playing.h"

static Theme theme;
static Settings settings;
static NowPlayingScreen screen;
static Color pixels[480 * 480];
static Color before[480 * 480];
static uint32_t samples[1024];

static Canvas start(int orientation, int size, int appearance, NowPlayingStyle style)
{
    fake_hal_reset();
    hal_display_set_rotation((HalDisplayRotation)orientation);
    meter_now_playing_reset();
    theme = BOARD_THEME;
    default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    settings = settings_defaults();
    settings_set_now_playing_style(&settings, style);
    now_playing_screen_init(&screen, (NowPlayingConfig){.theme = &theme, .settings = &settings});
    screen.status =
        (PlayerStatus){.has_track = true, .state = PLAYER_PLAYING, .duration_ms = 60000};
    strcpy(screen.title, "Night Transmission / a very long title beyond the horizon");
    strcpy(screen.artist, "The Midnight Transmission Orchestra");
    for (int frame = 0; frame < 512; frame++) {
        int32_t value = (frame & 8) != 0 ? 14000 : -14000;
        samples[frame * 2] = (uint32_t)value << 16;
        samples[frame * 2 + 1] = (uint32_t)(value / 2) << 16;
    }
    fake_hal_set_visual_buffer((AudioVisualBuffer){samples, 512, 480, 512, 44100});
    return canvas_make(pixels,
                       (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
}

static bool inside(Rect area, int x, int y)
{
    return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
}

static void art_and_text_fit_every_theme_orientation_and_size(void)
{
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Canvas canvas = start(orientation, size, appearance, NOW_PLAYING_VU);
                MeterNowPlayingLayout layout = meter_now_playing_layout(&theme);
                CHECK(layout.stage.y >= theme.metrics.header_height);
                CHECK(layout.stage.y + layout.stage.height < (orientation == 0 ? 196 : 367));
                CHECK(rect_is_empty(rect_intersection(layout.dials[0], layout.dials[1])));
                for (int line = 0; line < 2; line++) {
                    CHECK(layout.text[line].height >=
                          font_line_height(line == 0 ? theme.fonts.title : theme.fonts.body));
                    CHECK(layout.text[line].x + layout.text[line].width <=
                          theme.metrics.screen_width - theme.metrics.padding);
                }
                canvas_clear(&canvas, theme.colors.background);
                meter_now_playing_draw(&screen, &canvas);
            }
        }
    }
}

static void frames_change_only_the_meter_rectangles_and_decay_to_rest(void)
{
    for (int style = NOW_PLAYING_VU; style <= NOW_PLAYING_SPECTRUM; style++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            Canvas canvas =
                start(orientation, TEXT_SIZE_LARGE, APPEARANCE_NEON_NOIR, (NowPlayingStyle)style);
            FrameDiff diff = {0};
            screen.now_ms = 1000;
            canvas_clear(&canvas, theme.colors.background);
            meter_now_playing_draw(&screen, &canvas);
            memcpy(before, pixels, sizeof pixels);
            for (int frame = 0; frame < 5; frame++) {
                screen.now_ms += 80;
                meter_now_playing_animate(&screen, &diff, true);
            }
            canvas_clear(&canvas, theme.colors.background);
            meter_now_playing_draw(&screen, &canvas);
            CHECK(memcmp(before, pixels, sizeof pixels) != 0);
            MeterNowPlayingLayout layout = meter_now_playing_layout(&theme);
            for (int y = 0; y < canvas.bounds.height; y++) {
                for (int x = 0; x < canvas.bounds.width; x++) {
                    bool motion = style == NOW_PLAYING_SPECTRUM ? inside(layout.spectrum, x, y)
                                                                : inside(layout.dials[0], x, y) ||
                                                                      inside(layout.dials[1], x, y);
                    if (!motion) {
                        CHECK_EQUAL(before[y * canvas.bounds.width + x],
                                    pixels[y * canvas.bounds.width + x]);
                    }
                }
            }
            screen.status.state = PLAYER_PAUSED;
            for (int frame = 0; frame < 100; frame++) {
                screen.now_ms += 80;
                meter_now_playing_animate(&screen, &diff, true);
            }
            canvas_clear(&canvas, theme.colors.background);
            meter_now_playing_draw(&screen, &canvas);
            memcpy(before, pixels, sizeof pixels);
            screen.now_ms += 80;
            meter_now_playing_animate(&screen, &diff, true);
            canvas_clear(&canvas, theme.colors.background);
            meter_now_playing_draw(&screen, &canvas);
            CHECK(memcmp(before, pixels, sizeof pixels) == 0);
        }
    }
}

static void hidden_and_early_ticks_never_sample_or_send(void)
{
    start(0, TEXT_SIZE_MEDIUM, APPEARANCE_CLASSIC, NOW_PLAYING_SPECTRUM);
    FrameDiff diff = {0};
    screen.now_ms = 800;
    meter_now_playing_animate(&screen, &diff, false);
    CHECK_EQUAL(0, fake_hal_visual_reads());
    meter_now_playing_animate(&screen, &diff, true);
    CHECK_EQUAL(1, fake_hal_visual_reads());
    screen.now_ms += 1;
    meter_now_playing_animate(&screen, &diff, true);
    CHECK_EQUAL(1, fake_hal_visual_reads());
    screen.now_ms += 800;
    meter_now_playing_animate(&screen, &diff, false);
    CHECK_EQUAL(1, fake_hal_visual_reads());
    screen.status.state = PLAYER_PAUSED;
    meter_now_playing_animate(&screen, &diff, true);
    CHECK_EQUAL(1, fake_hal_visual_reads());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(art_and_text_fit_every_theme_orientation_and_size),
        TEST_CASE(frames_change_only_the_meter_rectangles_and_decay_to_rest),
        TEST_CASE(hidden_and_early_ticks_never_sample_or_send),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
