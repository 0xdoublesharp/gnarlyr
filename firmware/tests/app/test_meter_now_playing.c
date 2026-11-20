#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screens/meter_now_playing.h"
#include "screens/playing_work.h"
#include "screens/sunset_scene.h"
#include "ui/toast.h"

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

static SunsetScene scene(void)
{
    return sunset_scene_of(&theme, meter_now_playing_layout(&theme).stage);
}

static void draw_fresh(Canvas *canvas)
{
    canvas_clear(canvas, theme.colors.background);
    meter_now_playing_draw(&screen, canvas);
}

static void animate_frames(int frames)
{
    FrameDiff diff = {0};
    for (int frame = 0; frame < frames; frame++) {
        screen.now_ms += AUDIO_VISUAL_FRAME_MS;
        meter_now_playing_animate(&screen, &diff, true);
    }
}

static int solid_row(const SunsetScene *sun, int from)
{
    int y = from;
    while (sunset_scene_slat_gap(sun, y)) {
        y--;
    }
    return y;
}

static void art_and_text_fit_every_theme_orientation_and_size(void)
{
    for (int style = NOW_PLAYING_VU; style <= NOW_PLAYING_SPECTRUM; style++) {
        for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
            for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
                for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                    Canvas canvas = start(orientation, size, appearance, (NowPlayingStyle)style);
                    MeterNowPlayingLayout layout = meter_now_playing_layout(&theme);
                    CHECK(layout.stage.y >= theme.metrics.header_height);
                    CHECK(layout.stage.y + layout.stage.height < (orientation == 0 ? 196 : 367));
                    for (int line = 0; line < 2; line++) {
                        CHECK(layout.text[line].height >=
                              font_line_height(line == 0 ? theme.fonts.title : theme.fonts.body));
                        CHECK(layout.text[line].y + layout.text[line].height <=
                              (orientation == 0 ? 196 : 367));
                        CHECK(layout.text[line].x + layout.text[line].width <=
                              theme.metrics.screen_width - theme.metrics.padding);
                        CHECK(rect_is_empty(rect_intersection(layout.text[line], layout.stage)));
                    }
                    draw_fresh(&canvas);
                }
            }
        }
    }
}

static int partial_mismatches(Canvas *canvas)
{
    draw_fresh(canvas);
    int mismatches = 0;
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            mismatches += fake_hal_pixel(x, y) != pixels[y * canvas->bounds.width + x] ? 1 : 0;
        }
    }
    return mismatches;
}

static void check_only_the_stage_moves(Canvas *canvas)
{
    Rect stage = meter_now_playing_layout(&theme).stage;
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            if (!inside(stage, x, y)) {
                CHECK_EQUAL(before[y * canvas->bounds.width + x],
                            pixels[y * canvas->bounds.width + x]);
            }
        }
    }
}

static void partial_redraws_match_a_full_redraw_and_settle_at_rest(int style, int appearance,
                                                                   int orientation)
{
    Canvas canvas = start(orientation, TEXT_SIZE_LARGE, appearance, (NowPlayingStyle)style);
    screen.now_ms = 1000;
    draw_fresh(&canvas);
    memcpy(before, pixels, sizeof pixels);
    hal_display_write(0, 0, canvas.bounds.width, canvas.bounds.height, pixels);
    animate_frames(6);
    CHECK_EQUAL(0, partial_mismatches(&canvas));
    CHECK(memcmp(before, pixels, sizeof pixels) != 0);
    check_only_the_stage_moves(&canvas);
    screen.status.state = PLAYER_PAUSED;
    animate_frames(100);
    CHECK_EQUAL(0, partial_mismatches(&canvas));
    memcpy(before, pixels, sizeof pixels);
    animate_frames(1);
    draw_fresh(&canvas);
    CHECK(memcmp(before, pixels, sizeof pixels) == 0);
}

static void partial_redraws_match_a_full_redraw_in_every_theme_and_orientation(void)
{
    for (int style = NOW_PLAYING_VU; style <= NOW_PLAYING_SPECTRUM; style++) {
        for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
            for (int orientation = 0; orientation < 2; orientation++) {
                partial_redraws_match_a_full_redraw_and_settle_at_rest(style, appearance,
                                                                       orientation);
            }
        }
    }
}

static void the_sun_halves_light_from_the_horizon_with_each_channel(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Canvas canvas = start(orientation, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_VU);
        screen.now_ms = 1000;
        animate_frames(8);
        draw_fresh(&canvas);
        SunsetScene sun = scene();
        int left_top = sunset_scene_lit_top(&sun, playing_work_styles()->meter.shown[0]);
        int right_top = sunset_scene_lit_top(&sun, playing_work_styles()->meter.shown[1]);
        CHECK(left_top < right_top - 2);
        int both = solid_row(&sun, sun.horizon - 1);
        int only_left = solid_row(&sun, right_top - 1);
        CHECK(only_left > left_top);
        CHECK_EQUAL(canvas_pixel_at(&canvas, sun.sun.x - 3, both),
                    canvas_pixel_at(&canvas, sun.sun.x + 4, both));
        CHECK(canvas_pixel_at(&canvas, sun.sun.x - 3, only_left) !=
              canvas_pixel_at(&canvas, sun.sun.x + 4, only_left));
        Color lit = canvas_pixel_at(&canvas, sun.sun.x - 3, both);
        screen.status.state = PLAYER_PAUSED;
        animate_frames(100);
        draw_fresh(&canvas);
        CHECK(lit != canvas_pixel_at(&canvas, sun.sun.x - 3, both));
    }
}

static Color peak_pixel(Canvas *canvas, int side)
{
    SunsetScene sun = scene();
    int y = sunset_scene_top(&sun) + 2;
    draw_fresh(canvas);
    return canvas_pixel_at(canvas, side == 0 ? sun.sun.x - 2 : sun.sun.x + 3, y);
}

static bool peak_label_lit(Canvas *canvas, int side)
{
    Rect label = scene().peaks[side];
    for (int y = label.y; y < label.y + label.height; y++) {
        for (int x = label.x; x < label.x + label.width; x++) {
            if (canvas_pixel_at(canvas, x, y) == theme.colors.text) {
                return true;
            }
        }
    }
    return false;
}

static void a_peak_flashes_its_sun_crown_white_and_lights_peak_for_half_a_second(void)
{
    Canvas canvas = start(0, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_VU);
    for (int frame = 0; frame < 512; frame++) {
        samples[frame * 2] = (uint32_t)32767 << 16;
    }
    FrameDiff diff = {0};
    screen.now_ms = 1000;
    meter_now_playing_animate(&screen, &diff, true);
    CHECK_EQUAL(theme.colors.text, peak_pixel(&canvas, 0));
    CHECK(peak_label_lit(&canvas, 0));
    CHECK(theme.colors.text != peak_pixel(&canvas, 1));
    CHECK(!peak_label_lit(&canvas, 1));
    memset(samples, 0, sizeof samples);
    screen.status.state = PLAYER_PAUSED;
    screen.now_ms = 1400;
    meter_now_playing_animate(&screen, &diff, true);
    CHECK_EQUAL(theme.colors.text, peak_pixel(&canvas, 0));
    screen.now_ms = 1560;
    meter_now_playing_animate(&screen, &diff, true);
    CHECK(theme.colors.text != peak_pixel(&canvas, 0));
    CHECK(!peak_label_lit(&canvas, 0));
}

static void a_peak_that_lights_and_goes_out_is_restored_by_partial_redraws(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Canvas canvas = start(orientation, TEXT_SIZE_LARGE, APPEARANCE_SUNSET, NOW_PLAYING_VU);
        screen.now_ms = 1000;
        draw_fresh(&canvas);
        hal_display_write(0, 0, canvas.bounds.width, canvas.bounds.height, pixels);
        for (int frame = 0; frame < 512; frame++) {
            samples[frame * 2] = (uint32_t)32767 << 16;
            samples[frame * 2 + 1] = (uint32_t)32767 << 16;
        }
        animate_frames(2);
        CHECK_EQUAL(0, partial_mismatches(&canvas));
        CHECK(peak_label_lit(&canvas, 1));
        memset(samples, 0, sizeof samples);
        animate_frames(8);
        CHECK_EQUAL(0, partial_mismatches(&canvas));
        CHECK(!peak_label_lit(&canvas, 1));
    }
}

static void full_scale(void)
{
    for (int frame = 0; frame < 512; frame++) {
        samples[frame * 2] = (uint32_t)32767 << 16;
        samples[frame * 2 + 1] = (uint32_t)32767 << 16;
    }
}

static Canvas lit_peak_on_panel(int style)
{
    Canvas canvas = start(1, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, (NowPlayingStyle)style);
    screen.now_ms = 1000;
    draw_fresh(&canvas);
    hal_display_write(0, 0, canvas.bounds.width, canvas.bounds.height, pixels);
    full_scale();
    animate_frames(2);
    return canvas;
}

static void a_new_track_in_silence_repaints_the_crown_and_peak_it_inherited(void)
{
    Canvas canvas = lit_peak_on_panel(NOW_PLAYING_VU);
    CHECK_EQUAL(0, partial_mismatches(&canvas));
    CHECK(peak_label_lit(&canvas, 0));
    memset(samples, 0, sizeof samples);
    screen.status.track++;
    screen.text_changed_ms = screen.now_ms;
    animate_frames(1);
    CHECK_EQUAL(0, partial_mismatches(&canvas));
    CHECK(!peak_label_lit(&canvas, 0));
}

static void changing_the_style_in_place_repaints_the_stage(void)
{
    Canvas canvas = lit_peak_on_panel(NOW_PLAYING_VU);
    settings_set_now_playing_style(&settings, NOW_PLAYING_SPECTRUM);
    animate_frames(1);
    CHECK_EQUAL(0, partial_mismatches(&canvas));
    settings_set_now_playing_style(&settings, NOW_PLAYING_VU);
    animate_frames(1);
    CHECK_EQUAL(0, partial_mismatches(&canvas));
}

static void changing_the_theme_in_place_repaints_the_stage(void)
{
    Canvas canvas = lit_peak_on_panel(NOW_PLAYING_SPECTRUM);
    default_theme_set_appearance(&theme, APPEARANCE_CLASSIC);
    animate_frames(1);
    Rect stage = meter_now_playing_layout(&theme).stage;
    draw_fresh(&canvas);
    int mismatches = 0;
    for (int y = stage.y; y < stage.y + stage.height; y++) {
        for (int x = stage.x; x < stage.x + stage.width; x++) {
            mismatches += fake_hal_pixel(x, y) != pixels[y * canvas.bounds.width + x] ? 1 : 0;
        }
    }
    CHECK_EQUAL(0, mismatches);
}

static Toast toast;

static int toast_mismatches(Canvas *canvas)
{
    draw_fresh(canvas);
    toast_draw(&toast, canvas, &theme);
    int mismatches = 0;
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            mismatches += fake_hal_pixel(x, y) != pixels[y * canvas->bounds.width + x] ? 1 : 0;
        }
    }
    return mismatches;
}

static void a_toast_over_the_stage_survives_meter_motion_and_a_reset(int style, int orientation,
                                                                     int size)
{
    Canvas canvas = start(orientation, size, APPEARANCE_NEON_NOIR, (NowPlayingStyle)style);
    toast = (Toast){.visible = true};
    strcpy(toast.text, "Buttons locked");
    screen.config.toast = &toast;
    screen.now_ms = 1000;
    draw_fresh(&canvas);
    toast_draw(&toast, &canvas, &theme);
    hal_display_write(0, 0, canvas.bounds.width, canvas.bounds.height, pixels);
    full_scale();
    animate_frames(3);
    CHECK_EQUAL(0, toast_mismatches(&canvas));
    memset(samples, 0, sizeof samples);
    screen.status.track++;
    screen.text_changed_ms = screen.now_ms;
    animate_frames(3);
    CHECK_EQUAL(0, toast_mismatches(&canvas));
}

static void a_visible_toast_keeps_its_pixels_through_a_meter_reset_at_every_layout(void)
{
    for (int style = NOW_PLAYING_VU; style <= NOW_PLAYING_SPECTRUM; style++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                a_toast_over_the_stage_survives_meter_motion_and_a_reset(style, orientation, size);
            }
        }
    }
}

static void an_unlit_crown_stays_dark_after_long_uptime(void)
{
    Canvas canvas = start(0, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_VU);
    screen.now_ms = UINT32_C(0x90000000);
    CHECK(theme.colors.text != peak_pixel(&canvas, 0));
    CHECK(!peak_label_lit(&canvas, 0));
}

static void scale_marks_sit_beside_each_half_of_the_sun(void)
{
    Canvas canvas = start(1, TEXT_SIZE_SMALL, APPEARANCE_SUNSET, NOW_PLAYING_VU);
    draw_fresh(&canvas);
    SunsetScene sun = scene();
    Color tick = color_between(theme.colors.backdrop_bottom, theme.colors.text_secondary, 1, 2);
    for (int mark = 0; mark < SUNSET_MARKS; mark++) {
        int y = sunset_scene_mark(&sun, mark);
        int half = sunset_scene_half_width(&sun, y);
        CHECK_EQUAL(tick, canvas_pixel_at(&canvas, sun.sun.x - half - 4, y));
        CHECK_EQUAL(tick, canvas_pixel_at(&canvas, sun.sun.x + half + 4, y));
    }
}

static void spectrum_bars_have_white_tops_and_striped_reflections(void)
{
    Canvas canvas = start(0, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_SPECTRUM);
    screen.now_ms = 1000;
    draw_fresh(&canvas);
    memcpy(before, pixels, sizeof pixels);
    animate_frames(4);
    draw_fresh(&canvas);
    SunsetScene sun = scene();
    int tall = 0;
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        uint8_t level = playing_work_styles()->meter.bars[band];
        if (sunset_scene_reflection(&sun, level) < 4) {
            continue;
        }
        tall++;
        Rect bar = sunset_scene_bar(&sun, band);
        int x = bar.x + bar.width / 2;
        int width = canvas.bounds.width;
        CHECK_EQUAL(theme.colors.text,
                    canvas_pixel_at(&canvas, x, sunset_scene_bar_top(&sun, level)));
        CHECK(pixels[(sun.horizon + 1) * width + x] != before[(sun.horizon + 1) * width + x]);
        CHECK_EQUAL(before[(sun.horizon + 2) * width + x], pixels[(sun.horizon + 2) * width + x]);
    }
    CHECK(tall > 2);
}

static void the_floor_lines_brighten_with_the_bass(void)
{
    Canvas canvas = start(0, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_VU);
    SunsetScene sun = scene();
    int y = sunset_scene_floor_line(&sun, SUNSET_FLOOR_LINES - 1);
    for (int pulse = 0; pulse < SUNSET_PULSE_STEPS; pulse++) {
        playing_work_styles()->meter.pulse = (uint8_t)pulse;
        draw_fresh(&canvas);
        CHECK_EQUAL(
            color_between(theme.colors.divider, theme.colors.accent, 1 + pulse, SUNSET_PULSE_STEPS),
            canvas_pixel_at(&canvas, sun.stage.x + 1, y));
    }
}

static void the_floor_pulse_falls_one_step_every_other_tick(void)
{
    start(0, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_SPECTRUM);
    screen.status.state = PLAYER_PAUSED;
    screen.now_ms = 900;
    animate_frames(1);
    playing_work_styles()->meter.pulse = SUNSET_PULSE_STEPS - 1;
    static const uint8_t EXPECTED[] = {3, 2, 2, 1, 1, 0, 0};
    for (size_t tick = 0; tick < sizeof EXPECTED; tick++) {
        animate_frames(1);
        CHECK_EQUAL(EXPECTED[tick], playing_work_styles()->meter.pulse);
    }
}

static uint8_t vu_pulse_for_tone(double hertz)
{
    start(0, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, NOW_PLAYING_VU);
    for (int frame = 0; frame < 512; frame++) {
        int32_t value = (int32_t)(26000 * __builtin_sin(6.283185307179586 * hertz * frame / 44100));
        samples[frame * 2] = (uint32_t)value << 16;
        samples[frame * 2 + 1] = (uint32_t)value << 16;
    }
    screen.now_ms = 1000;
    uint8_t highest = 0;
    for (int tick = 0; tick < 12; tick++) {
        animate_frames(1);
        uint8_t pulse = playing_work_styles()->meter.pulse;
        highest = pulse > highest ? pulse : highest;
    }
    return highest;
}

static void the_vu_floor_pulses_with_bass_but_not_with_treble(void)
{
    CHECK_EQUAL(0, vu_pulse_for_tone(8000));
    CHECK(vu_pulse_for_tone(55) >= 2);
}

static int brightness(Color color)
{
    return ((color >> 11) & 31) * 2 + ((color >> 5) & 63) + (color & 31) * 2;
}

static void every_theme_draws_the_scenery_in_its_own_colours(void)
{
    for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
        Canvas canvas = start(0, TEXT_SIZE_MEDIUM, appearance, NOW_PLAYING_SPECTRUM);
        draw_fresh(&canvas);
        SunsetScene sun = scene();
        CHECK_EQUAL(theme.colors.backdrop_top, canvas_pixel_at(&canvas, sun.stage.x, sun.stage.y));
        CHECK_EQUAL(color_between(theme.colors.divider, theme.colors.accent, 1, SUNSET_PULSE_STEPS),
                    canvas_pixel_at(&canvas, sun.stage.x + 1, sunset_scene_floor_line(&sun, 3)));
        int y = solid_row(&sun, sun.sun.y);
        Color disc = canvas_pixel_at(&canvas, sun.sun.x, y);
        Color sky = canvas_pixel_at(&canvas, sun.stage.x + 2, y);
        CHECK(disc != sky);
        if (appearance == APPEARANCE_CLASSIC) {
            CHECK(brightness(disc) < brightness(sky));
        } else {
            CHECK(brightness(disc) > brightness(sky));
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

static void cover_flow_and_the_styles_take_turns_with_the_playing_work_area(void)
{
    playing_module_work.owner = PLAYING_WORK_NONE;
    CHECK(playing_work_claim_shuffle());
    CHECK(!playing_work_claim_shuffle());
    playing_module_work.as.shuffle[0] = COLOR_RGB(255, 255, 255);
    PlayingStyleWork *styles = playing_work_styles();
    CHECK_EQUAL(UINT32_MAX, styles->meter.frame);
    CHECK(!styles->meter.active);
    styles->meter.frame = 7;
    CHECK_EQUAL(7, playing_work_styles()->meter.frame);
    CHECK(playing_work_claim_shuffle());
    CHECK_EQUAL(UINT32_MAX, playing_work_styles()->meter.frame);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(cover_flow_and_the_styles_take_turns_with_the_playing_work_area),
        TEST_CASE(art_and_text_fit_every_theme_orientation_and_size),
        TEST_CASE(partial_redraws_match_a_full_redraw_in_every_theme_and_orientation),
        TEST_CASE(the_sun_halves_light_from_the_horizon_with_each_channel),
        TEST_CASE(a_peak_flashes_its_sun_crown_white_and_lights_peak_for_half_a_second),
        TEST_CASE(an_unlit_crown_stays_dark_after_long_uptime),
        TEST_CASE(a_peak_that_lights_and_goes_out_is_restored_by_partial_redraws),
        TEST_CASE(a_new_track_in_silence_repaints_the_crown_and_peak_it_inherited),
        TEST_CASE(changing_the_style_in_place_repaints_the_stage),
        TEST_CASE(changing_the_theme_in_place_repaints_the_stage),
        TEST_CASE(a_visible_toast_keeps_its_pixels_through_a_meter_reset_at_every_layout),
        TEST_CASE(scale_marks_sit_beside_each_half_of_the_sun),
        TEST_CASE(spectrum_bars_have_white_tops_and_striped_reflections),
        TEST_CASE(the_floor_lines_brighten_with_the_bass),
        TEST_CASE(the_floor_pulse_falls_one_step_every_other_tick),
        TEST_CASE(the_vu_floor_pulses_with_bass_but_not_with_treble),
        TEST_CASE(every_theme_draws_the_scenery_in_its_own_colours),
        TEST_CASE(hidden_and_early_ticks_never_sample_or_send),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
