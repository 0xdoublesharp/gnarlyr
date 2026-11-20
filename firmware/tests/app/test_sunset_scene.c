#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screens/meter_now_playing.h"
#include "screens/sunset_scene.h"

static Theme theme;

static SunsetScene scene_for(int orientation, int size)
{
    fake_hal_reset();
    hal_display_set_rotation((HalDisplayRotation)orientation);
    theme = BOARD_THEME;
    default_theme_set_appearance(&theme, APPEARANCE_NEON_NOIR);
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    return sunset_scene_of(&theme, meter_now_playing_layout(&theme).stage);
}

static bool inside(Rect outer, Rect inner)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

static bool clear_of_the_sun(const SunsetScene *scene, Rect area, int margin)
{
    for (int y = area.y; y < area.y + area.height; y++) {
        int half = sunset_scene_half_width(scene, y);
        if (half < 0) {
            continue;
        }
        int left = scene->sun.x - half - margin;
        int right = scene->sun.x + half + margin;
        if (area.x < right && area.x + area.width > left) {
            return false;
        }
    }
    return true;
}

static void the_sun_sits_on_the_horizon_inside_the_stage_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            SunsetScene scene = scene_for(orientation, size);
            Rect stage = scene.stage;
            CHECK(scene.horizon > stage.y + stage.height / 2);
            CHECK(scene.horizon < stage.y + stage.height - 24);
            CHECK(sunset_scene_top(&scene) > stage.y);
            CHECK(scene.sun.y < scene.horizon);
            CHECK(scene.sun.x - scene.radius >= stage.x + 10);
            CHECK(scene.sun.x + scene.radius <= stage.x + stage.width - 10);
            CHECK_EQUAL(-1, sunset_scene_half_width(&scene, scene.horizon));
            CHECK(sunset_scene_half_width(&scene, scene.horizon - 1) > scene.radius / 2);
        }
    }
}

static void the_labels_sit_inside_the_stage_clear_of_the_sun_and_each_other(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            SunsetScene scene = scene_for(orientation, size);
            for (int side = 0; side < 2; side++) {
                CHECK(inside(scene.stage, scene.channels[side]));
                CHECK(inside(scene.stage, scene.peaks[side]));
                CHECK(scene.channels[side].y + scene.channels[side].height <= scene.horizon - 2);
                CHECK(clear_of_the_sun(&scene, scene.channels[side], 4));
                CHECK(clear_of_the_sun(&scene, scene.peaks[side], 4));
                CHECK(rect_is_empty(rect_intersection(scene.channels[side], scene.peaks[side])));
            }
            CHECK(rect_is_empty(rect_intersection(scene.peaks[0], scene.peaks[1])));
        }
    }
}

static void portrait_puts_the_labels_in_the_sky_and_grows_the_sun(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        SunsetScene landscape = scene_for(ORIENTATION_LANDSCAPE, size);
        SunsetScene portrait = scene_for(ORIENTATION_PORTRAIT, size);
        CHECK(portrait.radius > landscape.radius);
        CHECK(portrait.radius * 2 >= portrait.stage.width * 3 / 4);
        for (int side = 0; side < 2; side++) {
            CHECK(portrait.peaks[side].y < portrait.stage.y + 12);
            CHECK(portrait.channels[side].y < sunset_scene_top(&portrait) + portrait.radius / 2);
            CHECK(landscape.channels[side].y + landscape.channels[side].height >
                  landscape.horizon - 12);
        }
    }
}

static void slats_open_wider_toward_the_horizon(void)
{
    SunsetScene scene = scene_for(ORIENTATION_LANDSCAPE, TEXT_SIZE_MEDIUM);
    int top = sunset_scene_top(&scene);
    int third = (scene.horizon - top) / 3;
    int upper = 0;
    int lower = 0;
    for (int y = top; y < top + third; y++) {
        upper += sunset_scene_slat_gap(&scene, y) ? 1 : 0;
    }
    for (int y = scene.horizon - third; y < scene.horizon; y++) {
        lower += sunset_scene_slat_gap(&scene, y) ? 1 : 0;
    }
    CHECK(lower > upper);
    CHECK(lower > third / 4);
    CHECK(!sunset_scene_slat_gap(&scene, top));
}

static void the_meter_lights_rows_from_the_horizon_up(void)
{
    SunsetScene scene = scene_for(ORIENTATION_PORTRAIT, TEXT_SIZE_SMALL);
    CHECK_EQUAL(scene.horizon, sunset_scene_lit_top(&scene, 0));
    CHECK_EQUAL(sunset_scene_top(&scene), sunset_scene_lit_top(&scene, 255));
    for (int level = 1; level < 256; level++) {
        CHECK(sunset_scene_lit_top(&scene, (uint8_t)level) <=
              sunset_scene_lit_top(&scene, (uint8_t)(level - 1)));
    }
    Rect left = sunset_scene_half(&scene, 0);
    Rect right = sunset_scene_half(&scene, 1);
    CHECK_EQUAL(scene.sun.x, left.x + left.width + 1);
    CHECK(right.x > scene.sun.x);
    CHECK_EQUAL(sunset_scene_top(&scene), left.y);
    CHECK_EQUAL(scene.horizon, left.y + left.height);
}

static void scale_marks_climb_from_minus_24_to_0_db(void)
{
    SunsetScene scene = scene_for(ORIENTATION_LANDSCAPE, TEXT_SIZE_LARGE);
    for (int mark = 1; mark < SUNSET_MARKS; mark++) {
        CHECK(sunset_scene_mark(&scene, mark) > sunset_scene_mark(&scene, mark - 1));
    }
    CHECK_EQUAL(sunset_scene_lit_top(&scene, audio_visual_db(32768)), sunset_scene_mark(&scene, 0));
    CHECK_EQUAL(sunset_scene_lit_top(&scene, audio_visual_db(2048)),
                sunset_scene_mark(&scene, SUNSET_MARKS - 1));
}

static void floor_lines_crowd_toward_the_horizon(void)
{
    SunsetScene scene = scene_for(ORIENTATION_LANDSCAPE, TEXT_SIZE_MEDIUM);
    Rect floor = sunset_scene_floor(&scene);
    CHECK_EQUAL(scene.horizon, floor.y);
    CHECK_EQUAL(scene.stage.y + scene.stage.height, floor.y + floor.height);
    int previous = scene.horizon;
    int gap = 0;
    for (int line = 0; line < SUNSET_FLOOR_LINES; line++) {
        int y = sunset_scene_floor_line(&scene, line);
        CHECK(y > previous);
        CHECK(y - previous >= gap);
        CHECK(y < floor.y + floor.height);
        gap = y - previous;
        previous = y;
    }
}

static void bars_share_the_width_and_rise_from_the_horizon(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        SunsetScene scene = scene_for(orientation, TEXT_SIZE_MEDIUM);
        CHECK(inside(scene.stage, scene.bars));
        for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
            Rect bar = sunset_scene_bar(&scene, band);
            CHECK(inside(scene.bars, bar));
            CHECK(bar.width >= 4);
            if (band > 0) {
                Rect before = sunset_scene_bar(&scene, band - 1);
                CHECK(bar.x >= before.x + before.width + 2);
            }
        }
        CHECK_EQUAL(scene.horizon - 1, sunset_scene_bar_top(&scene, 0));
        CHECK(sunset_scene_bar_top(&scene, 255) >= scene.bars.y + 4);
        CHECK_EQUAL(0, sunset_scene_reflection(&scene, 0));
        CHECK(sunset_scene_reflection(&scene, 255) < sunset_scene_floor(&scene).height);
        CHECK(sunset_scene_reflection(&scene, 255) > sunset_scene_reflection(&scene, 128));
    }
}

static void stars_stay_in_the_sky_clear_of_the_sun_and_labels(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            SunsetScene scene = scene_for(orientation, size);
            for (int star = 0; star < SUNSET_STARS; star++) {
                Point point = sunset_scene_star(&scene, star);
                Rect dot = {point.x - 1, point.y - 1, 3, 3};
                CHECK(inside(scene.stage, dot));
                CHECK(point.y < scene.horizon - 8);
                CHECK(clear_of_the_sun(&scene, dot, 3));
                for (int side = 0; side < 2; side++) {
                    CHECK(rect_is_empty(rect_intersection(dot, scene.channels[side])));
                    CHECK(rect_is_empty(rect_intersection(dot, scene.peaks[side])));
                }
            }
        }
    }
}

static void the_floor_pulse_jumps_up_and_falls_one_step_a_tick(void)
{
    CHECK_EQUAL(SUNSET_PULSE_STEPS - 1, sunset_scene_pulse(0, 255));
    CHECK_EQUAL(0, sunset_scene_pulse(0, 0));
    CHECK_EQUAL(2, sunset_scene_pulse(3, 0));
    CHECK_EQUAL(1, sunset_scene_pulse(2, 0));
    CHECK_EQUAL(0, sunset_scene_pulse(1, 0));
    uint8_t previous = 0;
    for (int energy = 0; energy < 256; energy++) {
        uint8_t pulse = sunset_scene_pulse(0, (uint8_t)energy);
        CHECK(pulse >= previous);
        previous = pulse;
    }
    CHECK(sunset_scene_pulse(0, 190) > 0);
    CHECK(sunset_scene_pulse(0, 190) < SUNSET_PULSE_STEPS - 1);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_sun_sits_on_the_horizon_inside_the_stage_at_every_size_and_orientation),
        TEST_CASE(the_labels_sit_inside_the_stage_clear_of_the_sun_and_each_other),
        TEST_CASE(portrait_puts_the_labels_in_the_sky_and_grows_the_sun),
        TEST_CASE(slats_open_wider_toward_the_horizon),
        TEST_CASE(the_meter_lights_rows_from_the_horizon_up),
        TEST_CASE(scale_marks_climb_from_minus_24_to_0_db),
        TEST_CASE(floor_lines_crowd_toward_the_horizon),
        TEST_CASE(bars_share_the_width_and_rise_from_the_horizon),
        TEST_CASE(stars_stay_in_the_sky_clear_of_the_sun_and_labels),
        TEST_CASE(the_floor_pulse_jumps_up_and_falls_one_step_a_tick),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
