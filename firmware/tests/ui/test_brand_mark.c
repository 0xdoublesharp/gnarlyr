#include "check.h"
#include "ui/brand_mark.h"
#include "ui/icons.h"

enum {
    SIDE = 140,
    BACKGROUND = 0x0841,
    LIGHT = 0xEF19
};

static Color pixels[SIDE * SIDE];

static Canvas blank(Color color)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, SIDE, SIDE});
    canvas_clear(&canvas, color);
    return canvas;
}

static Canvas drawn(int size, bool tile)
{
    Canvas canvas = blank(BACKGROUND);
    brand_mark_draw(&canvas, (BrandMark){{0, 0, size, size}, BACKGROUND, UINT8_MAX, tile});
    return canvas;
}

static int row_ink(const Canvas *canvas, int row, int size)
{
    int count = 0;
    for (int x = 0; x < size; x++) {
        count += canvas_pixel_at(canvas, x, row) != BACKGROUND ? 1 : 0;
    }
    return count;
}

static bool row_is(const Canvas *canvas, int row, int size, Color color)
{
    for (int x = 0; x < size; x++) {
        Color pixel = canvas_pixel_at(canvas, x, row);
        if (pixel != BACKGROUND && pixel != color) {
            return false;
        }
    }
    return true;
}

static int channel_gap(Color first, Color second, int shift, int mask)
{
    int gap = (first >> shift & mask) - (second >> shift & mask);
    return gap < 0 ? -gap : gap;
}

static bool near(Color first, Color second)
{
    return channel_gap(first, second, 11, 31) <= 1 && channel_gap(first, second, 5, 63) <= 2 &&
           channel_gap(first, second, 0, 31) <= 1;
}

static void the_header_discs_keep_two_horizon_gaps_on_whole_rows(void)
{
    static const uint8_t FOURTEEN[] = {9, 11};
    static const uint8_t TEN[] = {6, 8};
    for (int band = 0; band < 2; band++) {
        CHECK_EQUAL(FOURTEEN[band], brand_mark_gap(14, band).start);
        CHECK_EQUAL(FOURTEEN[band] + 1, brand_mark_gap(14, band).end);
        CHECK_EQUAL(TEN[band], brand_mark_gap(10, band).start);
        CHECK_EQUAL(TEN[band] + 1, brand_mark_gap(10, band).end);
    }
    Canvas canvas = drawn(14, false);
    CHECK_EQUAL(0, row_ink(&canvas, 9, 14));
    CHECK_EQUAL(0, row_ink(&canvas, 11, 14));
    CHECK(row_ink(&canvas, 10, 14) > 0);
    CHECK(row_ink(&canvas, 12, 14) > 0);
}

static void a_large_disc_cuts_its_gaps_where_the_website_mask_does(void)
{
    CHECK_EQUAL(59, brand_mark_gap(96, 0).start);
    CHECK_EQUAL(65, brand_mark_gap(96, 0).end);
    CHECK_EQUAL(77, brand_mark_gap(96, 1).start);
    CHECK_EQUAL(84, brand_mark_gap(96, 1).end);
}

static void the_sun_runs_through_the_website_gradient_from_top_to_bottom(void)
{
    CHECK_EQUAL(BRAND_MARK_SUN_TOP, brand_mark_sun_row(0, BRAND_MARK_STOP_SPAN / 2));
    CHECK_EQUAL(BRAND_MARK_SUN_MIDDLE,
                brand_mark_sun_row(BRAND_MARK_MIDDLE_STOP / 2, BRAND_MARK_STOP_SPAN / 2));
    CHECK(near(BRAND_MARK_SUN_BOTTOM,
               brand_mark_sun_row(BRAND_MARK_STOP_SPAN / 2 - 1, BRAND_MARK_STOP_SPAN / 2)));
    CHECK(near(BRAND_MARK_SUN_TOP, brand_mark_sun_row(0, 14)));
    CHECK(near(BRAND_MARK_SUN_BOTTOM, brand_mark_sun_row(13, 14)) ||
          !near(BRAND_MARK_SUN_TOP, brand_mark_sun_row(13, 14)));
}

static void a_header_disc_draws_whole_pixels_in_its_row_colour(void)
{
    for (int size = 10; size <= 14; size += 4) {
        Canvas canvas = drawn(size, false);
        for (int row = 0; row < size; row++) {
            CHECK(row_is(&canvas, row, size, brand_mark_sun_row(row, size)));
        }
    }
}

static void a_header_disc_is_round_and_symmetric(void)
{
    Canvas canvas = drawn(14, false);
    CHECK(row_ink(&canvas, 0, 14) < row_ink(&canvas, 5, 14));
    CHECK_EQUAL(14, row_ink(&canvas, 6, 14));
    for (int row = 0; row < 14; row++) {
        for (int x = 0; x < 7; x++) {
            CHECK_EQUAL(canvas_pixel_at(&canvas, x, row), canvas_pixel_at(&canvas, 13 - x, row));
        }
    }
}

static void a_large_disc_smooths_its_edges(void)
{
    Canvas canvas = drawn(96, false);
    int row = 20;
    Color pure = brand_mark_sun_row(row, 96);
    bool blended = false;
    for (int x = 0; x < 96; x++) {
        Color pixel = canvas_pixel_at(&canvas, x, row);
        blended |= pixel != BACKGROUND && pixel != pure;
    }
    CHECK(blended);
}

static void the_tile_frames_the_disc_with_the_website_outline(void)
{
    Canvas canvas = drawn(128, true);
    CHECK_EQUAL(BRAND_MARK_OUTLINE, canvas_pixel_at(&canvas, 64, 2));
    CHECK_EQUAL(BACKGROUND, canvas_pixel_at(&canvas, 0, 0));
    CHECK(near(BRAND_MARK_SUN_TOP, canvas_pixel_at(&canvas, 64, 17)));
    Color face = canvas_pixel_at(&canvas, 64, 8);
    CHECK(face != BACKGROUND && face != BRAND_MARK_OUTLINE);
    CHECK_EQUAL(96, brand_mark_disc_size(128, true));
    CHECK_EQUAL(14, brand_mark_disc_size(14, false));
}

static void only_a_light_surface_asks_for_the_tile(void)
{
    CHECK(brand_mark_needs_tile(COLOR_RGB(236, 225, 199)));
    CHECK(brand_mark_needs_tile(COLOR_RGB(214, 198, 162)));
    CHECK(!brand_mark_needs_tile(COLOR_RGB(36, 37, 34)));
    CHECK(!brand_mark_needs_tile(COLOR_RGB(22, 23, 27)));
    CHECK(!brand_mark_needs_tile(COLOR_RGB(57, 24, 47)));
}

static void a_mark_at_no_alpha_leaves_the_surface_it_fades_from(void)
{
    Canvas canvas = blank(LIGHT);
    brand_mark_draw(&canvas, (BrandMark){{0, 0, 64, 64}, LIGHT, 0, true});
    for (int index = 0; index < 64 * SIDE; index++) {
        CHECK_EQUAL(LIGHT, pixels[index]);
    }
}

static void the_mark_draws_only_inside_the_clip(void)
{
    Canvas whole = blank(BACKGROUND);
    Canvas strip = canvas_clipped(&whole, (Rect){0, 30, SIDE, 10});
    brand_mark_draw(&strip, (BrandMark){{0, 0, 96, 96}, BACKGROUND, UINT8_MAX, true});
    CHECK_EQUAL(0, row_ink(&whole, 29, 96));
    CHECK(row_ink(&whole, 30, 96) > 0);
    CHECK(row_ink(&whole, 39, 96) > 0);
    CHECK_EQUAL(0, row_ink(&whole, 40, 96));
}

static void the_logo_icon_draws_the_sun_disc_whatever_the_ink(void)
{
    Canvas canvas = blank(BACKGROUND);
    icon_draw(&canvas, ICON_LOGO, (Rect){0, 0, 14, 14}, (IconInk){0xFFFF, 0xFFFF});
    CHECK(near(BRAND_MARK_SUN_TOP, canvas_pixel_at(&canvas, 7, 0)));
    CHECK_EQUAL(0, row_ink(&canvas, 9, 14));
    for (int index = 0; index < 14 * SIDE; index++) {
        CHECK(pixels[index] != 0xFFFF);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_header_discs_keep_two_horizon_gaps_on_whole_rows),
        TEST_CASE(a_large_disc_cuts_its_gaps_where_the_website_mask_does),
        TEST_CASE(the_sun_runs_through_the_website_gradient_from_top_to_bottom),
        TEST_CASE(a_header_disc_draws_whole_pixels_in_its_row_colour),
        TEST_CASE(a_header_disc_is_round_and_symmetric),
        TEST_CASE(a_large_disc_smooths_its_edges),
        TEST_CASE(the_tile_frames_the_disc_with_the_website_outline),
        TEST_CASE(only_a_light_surface_asks_for_the_tile),
        TEST_CASE(a_mark_at_no_alpha_leaves_the_surface_it_fades_from),
        TEST_CASE(the_mark_draws_only_inside_the_clip),
        TEST_CASE(the_logo_icon_draws_the_sun_disc_whatever_the_ink),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
