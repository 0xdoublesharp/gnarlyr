#include "check.h"
#include "ui/icons.h"

enum {
    CANVAS_SIZE = 72,
    ORIGIN = 12,
    BACKGROUND = 0x0821,
    MAIN = 0xFFFF,
    ACCENT = 0xF800
};

static Color pixels[CANVAS_SIZE * CANVAS_SIZE];

static Canvas blank_canvas(void)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, CANVAS_SIZE, CANVAS_SIZE});
    canvas_clear(&canvas, BACKGROUND);
    return canvas;
}

static bool inside(Rect area, int x, int y)
{
    return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
}

static int count_ink(const Canvas *canvas, Rect area, bool within)
{
    int count = 0;
    for (int y = 0; y < CANVAS_SIZE; y++) {
        for (int x = 0; x < CANVAS_SIZE; x++) {
            bool drawn = canvas_pixel_at(canvas, x, y) != BACKGROUND;
            count += drawn && inside(area, x, y) == within ? 1 : 0;
        }
    }
    return count;
}

static int count_color(const Canvas *canvas, Color color)
{
    int count = 0;
    for (int index = 0; index < CANVAS_SIZE * CANVAS_SIZE; index++) {
        count += canvas->pixels[index] == color ? 1 : 0;
    }
    return count;
}

static void every_icon_draws_only_inside_its_area(void)
{
    for (int icon = ICON_NONE + 1; icon < ICON_COUNT; icon++) {
        Canvas canvas = blank_canvas();
        int size = icon_grid((IconId)icon);
        Rect area = {ORIGIN, ORIGIN, size, size};
        icon_draw(&canvas, (IconId)icon, area, (IconInk){MAIN, ACCENT});
        CHECK(count_ink(&canvas, area, true) > 0);
        CHECK_EQUAL(0, count_ink(&canvas, area, false));
    }
}

static void an_icon_scales_to_fill_a_larger_area(void)
{
    Canvas canvas = blank_canvas();
    Rect single = {ORIGIN, ORIGIN, ICON_SMALL_GRID, ICON_SMALL_GRID};
    icon_draw(&canvas, ICON_PAUSE, single, (IconInk){MAIN, MAIN});
    int small = count_color(&canvas, MAIN);
    canvas = blank_canvas();
    Rect doubled = {ORIGIN, ORIGIN, 2 * ICON_SMALL_GRID, 2 * ICON_SMALL_GRID};
    icon_draw(&canvas, ICON_PAUSE, doubled, (IconInk){MAIN, MAIN});
    CHECK_EQUAL(4 * small, count_color(&canvas, MAIN));
}

static void an_icon_stays_inside_the_canvas_clip(void)
{
    Canvas canvas = blank_canvas();
    Rect area = {ORIGIN, ORIGIN, ICON_LARGE_GRID, ICON_LARGE_GRID};
    Rect half = {ORIGIN, ORIGIN, ICON_LARGE_GRID / 2, ICON_LARGE_GRID};
    Canvas clipped = canvas_clipped(&canvas, half);
    icon_draw(&clipped, ICON_USB, area, (IconInk){MAIN, ACCENT});
    CHECK(count_ink(&canvas, half, true) > 0);
    CHECK_EQUAL(0, count_ink(&canvas, half, false));
}

static void accent_parts_use_the_accent_ink(void)
{
    Canvas canvas = blank_canvas();
    Rect area = {ORIGIN, ORIGIN, ICON_LARGE_GRID, ICON_LARGE_GRID};
    icon_draw(&canvas, ICON_NOW_PLAYING, area, (IconInk){MAIN, ACCENT});
    CHECK(count_color(&canvas, MAIN) > 0);
    CHECK(count_color(&canvas, ACCENT) > 0);
    canvas = blank_canvas();
    icon_draw(&canvas, ICON_NOW_PLAYING, area, (IconInk){MAIN, MAIN});
    CHECK_EQUAL(0, count_color(&canvas, ACCENT));
}

static void no_icon_draws_nothing(void)
{
    Canvas canvas = blank_canvas();
    Rect area = {ORIGIN, ORIGIN, ICON_LARGE_GRID, ICON_LARGE_GRID};
    icon_draw(&canvas, ICON_NONE, area, (IconInk){MAIN, ACCENT});
    icon_draw(&canvas, ICON_COUNT, area, (IconInk){MAIN, ACCENT});
    CHECK_EQUAL(0, count_ink(&canvas, area, true));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_icon_draws_only_inside_its_area),
        TEST_CASE(an_icon_scales_to_fill_a_larger_area),
        TEST_CASE(an_icon_stays_inside_the_canvas_clip),
        TEST_CASE(accent_parts_use_the_accent_ink),
        TEST_CASE(no_icon_draws_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
