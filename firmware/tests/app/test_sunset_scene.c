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

static void the_meter_bed_holds_two_rows_of_segments_inside_the_stage(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            SunsetScene scene = scene_for(orientation, size);
            CHECK(inside(scene.stage, scene.bed));
            CHECK(scene.bed.y + scene.bed.height < scene.stage.y + scene.stage.height - 8);
            for (int side = 0; side < 2; side++) {
                Rect row = sunset_scene_row(&scene, side);
                CHECK(inside(scene.bed, row));
                for (int segment = 0; segment < SUNSET_SEGMENTS; segment++) {
                    Rect cell = sunset_scene_segment(&scene, side, segment);
                    CHECK(inside(row, cell));
                    CHECK(cell.width >= 4);
                    if (segment > 0) {
                        Rect before = sunset_scene_segment(&scene, side, segment - 1);
                        CHECK_EQUAL(before.x + before.width + 2, cell.x);
                    }
                }
            }
            CHECK(rect_is_empty(
                rect_intersection(sunset_scene_row(&scene, 0), sunset_scene_row(&scene, 1))));
        }
    }
}

static void the_ridge_rises_from_the_horizon_across_the_stage(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        SunsetScene scene = scene_for(orientation, TEXT_SIZE_MEDIUM);
        CHECK_EQUAL(scene.stage.x, sunset_scene_ridge(&scene, 0).x);
        CHECK_EQUAL(scene.stage.x + scene.stage.width - 1,
                    sunset_scene_ridge(&scene, SUNSET_RIDGE_POINTS - 1).x);
        for (int point = 0; point < SUNSET_RIDGE_POINTS; point++) {
            Point at = sunset_scene_ridge(&scene, point);
            CHECK(at.y < scene.horizon);
            CHECK(at.y > sunset_scene_top(&scene));
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

static void a_level_lights_segments_from_none_to_all(void)
{
    CHECK_EQUAL(0, sunset_scene_lit(0));
    CHECK_EQUAL(SUNSET_SEGMENTS, sunset_scene_lit(255));
    for (int level = 1; level < 256; level++) {
        CHECK(sunset_scene_lit((uint8_t)level) >= sunset_scene_lit((uint8_t)(level - 1)));
    }
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

static void stars_stay_in_the_sky_clear_of_the_sun(void)
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

static void scene_uses_overrides_and_keeps_the_defaults_for_missing_targets(void)
{
    theme = BOARD_THEME;
    const Rect stage = {12, 25, 160, 200};
    SunsetScene defaults = sunset_scene_of(&theme, stage);
    ThemeVisualStyle style = {.scene_mask = 1u << THEME_SCENE_HORIZON,
                              .scene = {[THEME_SCENE_HORIZON] = 55}};
    theme.visual_style = &style;
    SunsetScene partial = sunset_scene_of(&theme, stage);
    CHECK_EQUAL(stage.y + 110, partial.horizon);
    CHECK_EQUAL(defaults.radius, partial.radius);
    CHECK_EQUAL(defaults.slats, partial.slats);
    CHECK_EQUAL(6, partial.floor_lines);
    CHECK_EQUAL(13, partial.rays);
    CHECK(partial.pulse);
    style.scene_mask = (1u << THEME_SCENE_TARGETS) - 1;
    style.scene[THEME_SCENE_SUN_RADIUS] = 25;
    style.scene[THEME_SCENE_FLOOR_LINES] = 8;
    SunsetScene scene = sunset_scene_of(&theme, stage);
    CHECK_EQUAL(40, scene.radius);
    CHECK_EQUAL(8, scene.floor_lines);
    CHECK_EQUAL(scene.horizon + 3 + 64 * 86 / 81, sunset_scene_floor_line(&scene, 7));
    CHECK_EQUAL(0, scene.rays);
    CHECK(!scene.pulse);
    for (int count = 0; count <= 8; count++) {
        style.scene[THEME_SCENE_SLATS] = (uint8_t)count;
        scene = sunset_scene_of(&theme, stage);
        int gaps = 0;
        bool previous = false;
        for (int y = scene.horizon - scene.radius; y < scene.horizon; y++) {
            bool gap = sunset_scene_slat_gap(&scene, y);
            gaps += gap && !previous;
            previous = gap;
        }
        CHECK_EQUAL(count, gaps);
    }
    style.scene[THEME_SCENE_HORIZON] = 75;
    style.scene[THEME_SCENE_FLOOR_LINES] = 10;
    scene = sunset_scene_of(&theme, (Rect){12, 25, 236, 124});
    for (int line = 1; line < scene.floor_lines; line++) {
        CHECK(sunset_scene_floor_line(&scene, line) > sunset_scene_floor_line(&scene, line - 1));
    }
    theme.visual_style = NULL;
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_sun_sits_on_the_horizon_inside_the_stage_at_every_size_and_orientation),
        TEST_CASE(the_meter_bed_holds_two_rows_of_segments_inside_the_stage),
        TEST_CASE(the_ridge_rises_from_the_horizon_across_the_stage),
        TEST_CASE(slats_open_wider_toward_the_horizon),
        TEST_CASE(a_level_lights_segments_from_none_to_all),
        TEST_CASE(floor_lines_crowd_toward_the_horizon),
        TEST_CASE(bars_share_the_width_and_rise_from_the_horizon),
        TEST_CASE(stars_stay_in_the_sky_clear_of_the_sun),
        TEST_CASE(the_floor_pulse_jumps_up_and_falls_one_step_a_tick),
        TEST_CASE(scene_uses_overrides_and_keeps_the_defaults_for_missing_targets),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
