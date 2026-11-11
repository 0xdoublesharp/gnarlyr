#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "gfx/wireframe.h"

enum {
    WIDTH = 40,
    HEIGHT = 30,
    STRIP = 7,
    BACKGROUND = 0x0000,
    INK = 0xFFFF
};

static Color whole[WIDTH * HEIGHT];
static Color pieced[WIDTH * HEIGHT];
static Color strip[WIDTH * STRIP];

static const WirePlace UNIT = {0, WIREFRAME_FULL_SIZE, 0, 0, 0};

static WireView view_at(uint8_t yaw, uint8_t pitch)
{
    return (WireView){.yaw = yaw, .pitch = pitch, .center = {50, 40}, .radius = 32};
}

static void clear(Color *pixels, int count)
{
    for (int index = 0; index < count; index++) {
        pixels[index] = BACKGROUND;
    }
}

static WirePoint at(int x, int y)
{
    return (WirePoint){x * WIREFRAME_SUBPIXEL, y * WIREFRAME_SUBPIXEL, WIREFRAME_NEAREST};
}

static void the_sine_table_matches_the_sine_at_every_angle(void)
{
    for (int angle = 0; angle < WIREFRAME_TURN; angle++) {
        double radians = (double)angle * 2.0 * M_PI / (double)WIREFRAME_TURN;
        double expected = sin(radians) * (double)WIREFRAME_ONE;
        CHECK(fabs(wireframe_sin((uint8_t)angle) - expected) <= 0.5);
    }
}

static void the_cosine_is_the_sine_a_quarter_turn_later(void)
{
    CHECK_EQUAL(WIREFRAME_ONE, wireframe_cos(0));
    CHECK_EQUAL(-WIREFRAME_ONE, wireframe_cos(WIREFRAME_TURN / 2));
    for (int angle = 0; angle < WIREFRAME_TURN; angle++) {
        CHECK_EQUAL(wireframe_sin((uint8_t)(angle + WIREFRAME_QUARTER)),
                    wireframe_cos((uint8_t)angle));
    }
}

static void the_origin_projects_to_the_view_center_at_middle_depth(void)
{
    WireView view = view_at(37, 91);
    WirePoint point = wireframe_project((WireVertex){0, 0, 0}, UNIT, &view);
    CHECK_EQUAL(50 * WIREFRAME_SUBPIXEL, point.x);
    CHECK_EQUAL(40 * WIREFRAME_SUBPIXEL, point.y);
    CHECK_EQUAL(128, point.depth);
}

static void a_vertex_at_full_radius_lands_a_view_radius_from_the_center(void)
{
    WireView view = view_at(0, 0);
    WirePoint right = wireframe_project((WireVertex){WIREFRAME_RADIUS, 0, 0}, UNIT, &view);
    WirePoint below = wireframe_project((WireVertex){0, WIREFRAME_RADIUS, 0}, UNIT, &view);
    int reach = 32 * WIREFRAME_SUBPIXEL * WIREFRAME_RADIUS / (WIREFRAME_RADIUS + 1);
    CHECK_EQUAL(50 * WIREFRAME_SUBPIXEL + reach, right.x);
    CHECK_EQUAL(40 * WIREFRAME_SUBPIXEL, right.y);
    CHECK_EQUAL(40 * WIREFRAME_SUBPIXEL + reach, below.y);
}

static void a_quarter_turn_of_yaw_swings_width_into_depth(void)
{
    WireView view = view_at(WIREFRAME_QUARTER, 0);
    WirePoint point = wireframe_project((WireVertex){WIREFRAME_RADIUS, 0, 0}, UNIT, &view);
    CHECK(abs(point.x - 50 * WIREFRAME_SUBPIXEL) <= 1);
    CHECK(point.depth < 8);
}

static void nearer_points_spread_further_from_the_center(void)
{
    WireView view = view_at(0, 0);
    WirePoint near = wireframe_project((WireVertex){100, 0, -100}, UNIT, &view);
    WirePoint far = wireframe_project((WireVertex){100, 0, 100}, UNIT, &view);
    CHECK(near.x > far.x);
    CHECK(near.depth > far.depth);
}

static void a_place_spins_scales_and_moves_a_part_before_the_view(void)
{
    WireView view = view_at(0, 0);
    WirePlace place = {WIREFRAME_QUARTER, WIREFRAME_FULL_SIZE / 2, 0, -WIREFRAME_RADIUS, 0};
    WirePoint point = wireframe_project((WireVertex){WIREFRAME_RADIUS, 0, 0}, place, &view);
    CHECK(abs(point.x - 50 * WIREFRAME_SUBPIXEL) <= 1);
    CHECK(abs(point.y - 24 * WIREFRAME_SUBPIXEL) <= 2);
}

static void a_line_on_whole_pixels_lights_each_pixel_once(void)
{
    clear(whole, WIDTH * HEIGHT);
    Canvas canvas = canvas_make(whole, (Rect){0, 0, WIDTH, HEIGHT});
    wireframe_line(&canvas, at(2, 5), at(9, 5), (WireStroke){INK, COLOR_ALPHA_OPAQUE});
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            bool lit = y == 5 && x >= 2 && x <= 9;
            CHECK_EQUAL(lit ? INK : BACKGROUND, whole[y * WIDTH + x]);
        }
    }
}

static void a_line_between_pixels_shares_its_ink_between_neighbours(void)
{
    clear(whole, WIDTH * HEIGHT);
    Canvas canvas = canvas_make(whole, (Rect){0, 0, WIDTH, HEIGHT});
    WirePoint from = {2 * WIREFRAME_SUBPIXEL, 5 * WIREFRAME_SUBPIXEL + 8, 0};
    WirePoint to = {9 * WIREFRAME_SUBPIXEL, 5 * WIREFRAME_SUBPIXEL + 8, 0};
    wireframe_line(&canvas, from, to, (WireStroke){INK, COLOR_ALPHA_OPAQUE});
    Color upper = whole[5 * WIDTH + 4];
    Color lower = whole[6 * WIDTH + 4];
    CHECK(upper != BACKGROUND && upper != INK);
    CHECK_EQUAL(upper, lower);
}

static void draw_lines(Canvas *canvas)
{
    WireStroke stroke = {INK, COLOR_ALPHA_OPAQUE};
    wireframe_line(canvas, (WirePoint){-60, 13, 0}, (WirePoint){700, 450, 0}, stroke);
    wireframe_line(canvas, (WirePoint){600, -40, 0}, (WirePoint){90, 520, 0}, stroke);
    wireframe_line(canvas, (WirePoint){33, 200, 0}, (WirePoint){620, 211, 0}, stroke);
}

static void lines_drawn_strip_by_strip_match_lines_drawn_whole(void)
{
    clear(whole, WIDTH * HEIGHT);
    Canvas canvas = canvas_make(whole, (Rect){0, 0, WIDTH, HEIGHT});
    draw_lines(&canvas);
    for (int top = 0; top < HEIGHT; top += STRIP) {
        int rows = HEIGHT - top < STRIP ? HEIGHT - top : STRIP;
        clear(strip, WIDTH * STRIP);
        Canvas piece = canvas_make(strip, (Rect){0, top, WIDTH, rows});
        draw_lines(&piece);
        memcpy(&pieced[top * WIDTH], strip, sizeof(Color) * (size_t)(WIDTH * rows));
    }
    CHECK(memcmp(whole, pieced, sizeof whole) == 0);
}

static void a_line_never_draws_outside_the_clip(void)
{
    clear(whole, WIDTH * HEIGHT);
    Canvas canvas = canvas_make(whole, (Rect){0, 0, WIDTH, HEIGHT});
    Rect inside = {10, 8, 12, 9};
    Canvas clipped = canvas_clipped(&canvas, inside);
    draw_lines(&clipped);
    int lit = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            bool within = x >= 10 && x < 22 && y >= 8 && y < 17;
            CHECK(within || whole[y * WIDTH + x] == BACKGROUND);
            lit += whole[y * WIDTH + x] != BACKGROUND;
        }
    }
    CHECK(lit > 0);
}

static void a_line_wholly_outside_the_clip_draws_nothing(void)
{
    clear(whole, WIDTH * HEIGHT);
    Canvas canvas = canvas_make(whole, (Rect){0, 0, WIDTH, HEIGHT});
    Canvas clipped = canvas_clipped(&canvas, (Rect){0, 20, WIDTH, 5});
    wireframe_line(&clipped, at(0, 2), at(39, 9), (WireStroke){INK, COLOR_ALPHA_OPAQUE});
    for (int index = 0; index < WIDTH * HEIGHT; index++) {
        CHECK_EQUAL(BACKGROUND, whole[index]);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_sine_table_matches_the_sine_at_every_angle),
        TEST_CASE(the_cosine_is_the_sine_a_quarter_turn_later),
        TEST_CASE(the_origin_projects_to_the_view_center_at_middle_depth),
        TEST_CASE(a_vertex_at_full_radius_lands_a_view_radius_from_the_center),
        TEST_CASE(a_quarter_turn_of_yaw_swings_width_into_depth),
        TEST_CASE(nearer_points_spread_further_from_the_center),
        TEST_CASE(a_place_spins_scales_and_moves_a_part_before_the_view),
        TEST_CASE(a_line_on_whole_pixels_lights_each_pixel_once),
        TEST_CASE(a_line_between_pixels_shares_its_ink_between_neighbours),
        TEST_CASE(lines_drawn_strip_by_strip_match_lines_drawn_whole),
        TEST_CASE(a_line_never_draws_outside_the_clip),
        TEST_CASE(a_line_wholly_outside_the_clip_draws_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
