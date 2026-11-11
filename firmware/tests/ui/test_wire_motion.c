#include <string.h>

#include "check.h"
#include "ui/wire_motion.h"
#include "ui_test_theme.h"

enum {
    WIDTH = 160,
    HEIGHT = 120,
    STRIP = 23,
    START_MS = 5000,
    OWNER_A = 1,
    OWNER_B = 2
};

static Color whole[WIDTH * HEIGHT];
static Color pieced[WIDTH * HEIGHT];
static Color strip[WIDTH * STRIP];
static const char OWNERS[3];
static const Rect SPACE = {10, 6, 140, 108};

static Theme theme(void)
{
    Theme styled = TEST_THEME;
    styled.colors.accent = 0x07FF;
    return styled;
}

static void clear(Color *pixels, int count)
{
    for (int index = 0; index < count; index++) {
        pixels[index] = TEST_THEME.colors.background;
    }
}

static WireMotion shown(WireShape shape, uint8_t turn)
{
    WireMotion motion = {0};
    wire_motion_run(&motion, &OWNERS[OWNER_A], shape, START_MS);
    wire_motion_tick(&motion, START_MS + WIRE_MOTION_DELAY_MS + (uint32_t)turn * 16u);
    return motion;
}

static int draw_whole(WireMotion *motion)
{
    Theme styled = theme();
    clear(whole, WIDTH * HEIGHT);
    Canvas canvas = canvas_make(whole, (Rect){0, 0, WIDTH, HEIGHT});
    wire_motion_draw(motion, &canvas, &styled, SPACE);
    int lit = 0;
    for (int index = 0; index < WIDTH * HEIGHT; index++) {
        lit += whole[index] != TEST_THEME.colors.background;
    }
    return lit;
}

static void check_model(const WireModel *model)
{
    bool used[256] = {false};
    for (int edge = 0; edge < model->edge_count; edge++) {
        WireEdge joined = model->edges[edge];
        CHECK(joined.from < model->vertex_count);
        CHECK(joined.to < model->vertex_count);
        CHECK(joined.from != joined.to);
        used[joined.from] = used[joined.to] = true;
        for (int other = 0; other < edge; other++) {
            WireEdge earlier = model->edges[other];
            bool same = earlier.from == joined.from && earlier.to == joined.to;
            bool reversed = earlier.from == joined.to && earlier.to == joined.from;
            CHECK(!same && !reversed);
        }
    }
    for (int vertex = 0; vertex < model->vertex_count; vertex++) {
        WireVertex point = model->vertices[vertex];
        int length = point.x * point.x + point.y * point.y + point.z * point.z;
        CHECK(used[vertex]);
        CHECK(length <= WIREFRAME_RADIUS * WIREFRAME_RADIUS + WIREFRAME_RADIUS);
    }
}

static void every_model_joins_distinct_used_vertices_inside_the_unit_sphere(void)
{
    check_model(&WIRE_MODEL_CASSETTE);
    check_model(&WIRE_MODEL_REEL);
    check_model(&WIRE_MODEL_RING);
    check_model(&WIRE_MODEL_ICOSAHEDRON);
    CHECK_EQUAL(12, WIRE_MODEL_ICOSAHEDRON.vertex_count);
    CHECK_EQUAL(30, WIRE_MODEL_ICOSAHEDRON.edge_count);
}

static void a_motion_waits_before_its_first_frame(void)
{
    WireMotion motion = {0};
    CHECK(!wire_motion_due(&motion, START_MS));
    wire_motion_run(&motion, &OWNERS[OWNER_A], WIRE_SHAPE_DISC, START_MS);
    CHECK(!wire_motion_due(&motion, START_MS + WIRE_MOTION_DELAY_MS - 1));
    CHECK(wire_motion_due(&motion, START_MS + WIRE_MOTION_DELAY_MS));
    CHECK_EQUAL(0, draw_whole(&motion));
    CHECK(!wire_motion_shown(&motion, &OWNERS[OWNER_A]));
}

static void frames_come_no_faster_than_the_frame_interval(void)
{
    WireMotion motion = shown(WIRE_SHAPE_DISC, 0);
    uint32_t ticked = motion.ticked_ms;
    CHECK(!wire_motion_due(&motion, ticked + WIRE_MOTION_FRAME_MS - 1));
    CHECK(wire_motion_due(&motion, ticked + WIRE_MOTION_FRAME_MS));
    CHECK(1000 / WIRE_MOTION_FRAME_MS <= 15);
    CHECK(1000 / WIRE_MOTION_FRAME_MS >= 12);
}

static void the_turn_follows_the_clock_not_the_frame_count(void)
{
    WireMotion motion = shown(WIRE_SHAPE_ICOSAHEDRON, 0);
    wire_motion_tick(&motion, START_MS + 1600);
    CHECK_EQUAL(100, motion.turn);
}

static void running_again_for_the_same_owner_and_shape_keeps_the_clock(void)
{
    WireMotion motion = shown(WIRE_SHAPE_CASSETTE, 3);
    wire_motion_run(&motion, &OWNERS[OWNER_A], WIRE_SHAPE_CASSETTE, START_MS + 900);
    CHECK_EQUAL(START_MS, motion.started_ms);
    CHECK(motion.ticked);
    wire_motion_run(&motion, &OWNERS[OWNER_A], WIRE_SHAPE_DISC, START_MS + 900);
    CHECK_EQUAL(START_MS + 900, motion.started_ms);
    CHECK(!motion.ticked);
}

static void only_the_owner_ends_a_motion(void)
{
    WireMotion motion = shown(WIRE_SHAPE_DISC, 0);
    wire_motion_end(&motion, &OWNERS[OWNER_B]);
    CHECK(wire_motion_shown(&motion, &OWNERS[OWNER_A]));
    CHECK(!wire_motion_shown(&motion, &OWNERS[OWNER_B]));
    wire_motion_end(&motion, &OWNERS[OWNER_A]);
    CHECK(!wire_motion_shown(&motion, &OWNERS[OWNER_A]));
    CHECK(!wire_motion_due(&motion, START_MS + 10000));
    CHECK_EQUAL(0, draw_whole(&motion));
}

static void the_fitted_square_is_centred_capped_and_dropped_when_too_small(void)
{
    Rect fitted = wire_motion_fit((Rect){0, 0, 300, 200});
    CHECK_EQUAL(WIRE_MOTION_MAX_SIDE, fitted.width);
    CHECK_EQUAL(WIRE_MOTION_MAX_SIDE, fitted.height);
    CHECK_EQUAL((300 - WIRE_MOTION_MAX_SIDE) / 2, fitted.x);
    CHECK_EQUAL((200 - WIRE_MOTION_MAX_SIDE) / 2, fitted.y);
    fitted = wire_motion_fit((Rect){4, 4, 200, 60});
    CHECK_EQUAL(60, fitted.width);
    CHECK_EQUAL(4, fitted.y);
    CHECK(rect_is_empty(wire_motion_fit((Rect){0, 0, 200, WIRE_MOTION_MIN_SIDE - 1})));
}

static void every_shape_draws_only_inside_its_square_at_every_turn(void)
{
    for (int shape = 0; shape < WIRE_SHAPE_COUNT; shape++) {
        for (int turn = 0; turn < WIREFRAME_TURN; turn += 7) {
            WireMotion motion = shown((WireShape)shape, (uint8_t)turn);
            CHECK(draw_whole(&motion) > 0);
            Rect area = motion.area;
            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < WIDTH; x++) {
                    bool inside = x >= area.x && x < area.x + area.width && y >= area.y &&
                                  y < area.y + area.height;
                    CHECK(inside || whole[y * WIDTH + x] == TEST_THEME.colors.background);
                }
            }
        }
    }
}

static void a_motion_drawn_strip_by_strip_matches_one_drawn_whole(void)
{
    Theme styled = theme();
    for (int shape = 0; shape < WIRE_SHAPE_COUNT; shape++) {
        WireMotion motion = shown((WireShape)shape, 41);
        draw_whole(&motion);
        for (int top = 0; top < HEIGHT; top += STRIP) {
            int rows = HEIGHT - top < STRIP ? HEIGHT - top : STRIP;
            clear(strip, WIDTH * STRIP);
            Canvas piece = canvas_make(strip, (Rect){0, top, WIDTH, rows});
            wire_motion_draw(&motion, &piece, &styled, SPACE);
            memcpy(&pieced[top * WIDTH], strip, sizeof(Color) * (size_t)(WIDTH * rows));
        }
        CHECK(memcmp(whole, pieced, sizeof whole) == 0);
    }
}

static void progress_is_a_clamped_percentage_and_no_total_means_endless(void)
{
    WireMotion motion = shown(WIRE_SHAPE_CASSETTE, 0);
    CHECK_EQUAL(WIRE_MOTION_ENDLESS, motion.percent);
    wire_motion_progress(&motion, 1, 4);
    CHECK_EQUAL(25, motion.percent);
    wire_motion_progress(&motion, 9, 4);
    CHECK_EQUAL(100, motion.percent);
    wire_motion_progress(&motion, 3, 0);
    CHECK_EQUAL(WIRE_MOTION_ENDLESS, motion.percent);
}

static void a_gauge_lights_more_of_the_model_as_progress_grows(void)
{
    WireMotion motion = shown(WIRE_SHAPE_ICOSAHEDRON, 20);
    wire_motion_progress(&motion, 0, 100);
    draw_whole(&motion);
    memcpy(pieced, whole, sizeof whole);
    wire_motion_progress(&motion, 100, 100);
    draw_whole(&motion);
    int brighter = 0;
    for (int index = 0; index < WIDTH * HEIGHT; index++) {
        brighter += whole[index] > pieced[index];
    }
    CHECK(brighter > 50);
}

static void tape_moves_from_the_left_pack_to_the_right_as_progress_grows(void)
{
    WireMotion motion = shown(WIRE_SHAPE_CASSETTE, 0);
    wire_motion_progress(&motion, 0, 100);
    draw_whole(&motion);
    memcpy(pieced, whole, sizeof whole);
    wire_motion_progress(&motion, 100, 100);
    draw_whole(&motion);
    int left = 0;
    int right = 0;
    int middle = motion.area.x + motion.area.width / 2;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int index = y * WIDTH + x;
            bool differs = whole[index] != pieced[index];
            left += differs && x < middle;
            right += differs && x >= middle;
        }
    }
    CHECK(left > 0);
    CHECK(right > 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_model_joins_distinct_used_vertices_inside_the_unit_sphere),
        TEST_CASE(a_motion_waits_before_its_first_frame),
        TEST_CASE(frames_come_no_faster_than_the_frame_interval),
        TEST_CASE(the_turn_follows_the_clock_not_the_frame_count),
        TEST_CASE(running_again_for_the_same_owner_and_shape_keeps_the_clock),
        TEST_CASE(only_the_owner_ends_a_motion),
        TEST_CASE(the_fitted_square_is_centred_capped_and_dropped_when_too_small),
        TEST_CASE(every_shape_draws_only_inside_its_square_at_every_turn),
        TEST_CASE(a_motion_drawn_strip_by_strip_matches_one_drawn_whole),
        TEST_CASE(progress_is_a_clamped_percentage_and_no_total_means_endless),
        TEST_CASE(a_gauge_lights_more_of_the_model_as_progress_grows),
        TEST_CASE(tape_moves_from_the_left_pack_to_the_right_as_progress_grows),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
