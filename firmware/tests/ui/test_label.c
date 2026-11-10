#include <string.h>

#include "check.h"
#include "tiny_font.h"
#include "ui/label.h"

enum {
    WIDTH = 10,
    HEIGHT = 4,
    BACKGROUND = 0x0000,
    INK = 0xFFFF
};

static Color pixels[WIDTH * HEIGHT];

static const TextStyle INK_STYLE = {.font = &TINY_FONT, .color = INK};

static Canvas blank_canvas(void)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, WIDTH, HEIGHT});
    canvas_clear(&canvas, BACKGROUND);
    return canvas;
}

static void text_that_fits_is_kept_whole(void)
{
    LabelFit fit = label_fit(&TINY_FONT, "AB", 7);
    CHECK_EQUAL(2, fit.byte_count);
    CHECK(!fit.ellipsis);
}

static void text_too_wide_is_cut_to_make_room_for_the_ellipsis(void)
{
    LabelFit fit = label_fit(&TINY_FONT, "AB", 6);
    CHECK_EQUAL(1, fit.byte_count);
    CHECK(fit.ellipsis);
}

static void the_cut_never_splits_a_character(void)
{
    LabelFit fit = label_fit(&TINY_FONT,
                             "\xC3\xA9"
                             "A",
                             5);
    CHECK_EQUAL(2, fit.byte_count);
    CHECK(fit.ellipsis);
}

static void a_width_too_small_for_any_character_keeps_only_the_ellipsis(void)
{
    LabelFit fit = label_fit(&TINY_FONT, "AB", 3);
    CHECK_EQUAL(0, fit.byte_count);
    CHECK(fit.ellipsis);
}

static void the_baseline_centres_the_line_in_its_area(void)
{
    CHECK_EQUAL(4, label_baseline(&TINY_FONT, (Rect){0, 0, 10, 7}));
    CHECK_EQUAL(12, label_baseline(&TINY_FONT, (Rect){0, 8, 10, 7}));
}

static void a_label_never_paints_outside_its_area(void)
{
    Canvas canvas = blank_canvas();
    label_draw(&canvas, INK_STYLE, (Label){.area = {2, 0, 4, 4}, .text = "AB"});
    int inked_inside = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            bool inside = x >= 2 && x < 6;
            Color pixel = canvas_pixel_at(&canvas, x, y);
            CHECK(inside || pixel == BACKGROUND);
            inked_inside += inside && pixel != BACKGROUND ? 1 : 0;
        }
    }
    CHECK(inked_inside > 0);
}

static void a_scrolling_label_shifts_its_text_and_repeats_it(void)
{
    Canvas canvas = blank_canvas();
    Label label = {.area = {0, 0, WIDTH, HEIGHT}, .text = "A"};
    label_draw_scrolling(&canvas, INK_STYLE, label, (Marquee){.offset = 1, .gap = 2});
    CHECK_EQUAL(INK, canvas_pixel_at(&canvas, 0, 0));
    CHECK_EQUAL(BACKGROUND, canvas_pixel_at(&canvas, 1, 0));
    CHECK_EQUAL(INK, canvas_pixel_at(&canvas, 6, 0));
}

static void a_centred_area_sits_in_the_middle_of_its_bounds(void)
{
    Rect area = label_centered_area(&TINY_FONT, (Rect){10, 3, 20, 5}, "AB");
    CHECK_EQUAL(16, area.x);
    CHECK_EQUAL(7, area.width);
    CHECK_EQUAL(3, area.y);
    CHECK_EQUAL(5, area.height);
}

static void a_centred_area_is_never_wider_than_its_bounds(void)
{
    Rect area = label_centered_area(&TINY_FONT, (Rect){10, 3, 5, 5}, "AB");
    CHECK_EQUAL(10, area.x);
    CHECK_EQUAL(5, area.width);
}

enum {
    WIDE = 24,
    TALL = 12,
    LINE = 3
};

static Color wide_pixels[2][WIDE * TALL];

static Canvas wide_canvas(int buffer)
{
    Canvas canvas = canvas_make(wide_pixels[buffer], (Rect){0, 0, WIDE, TALL});
    canvas_clear(&canvas, BACKGROUND);
    return canvas;
}

static bool row_has_ink(const Canvas *canvas, int y)
{
    for (int x = 0; x < WIDE; x++) {
        if (canvas_pixel_at(canvas, x, y) != BACKGROUND) {
            return true;
        }
    }
    return false;
}

static int inked_lines(const Canvas *canvas)
{
    int lines = 0;
    for (int y = 0; y < TALL; y += LINE) {
        lines += row_has_ink(canvas, y) || row_has_ink(canvas, y + 1) || row_has_ink(canvas, y + 2);
    }
    return lines;
}

static int first_ink(const Canvas *canvas, int y)
{
    for (int x = 0; x < WIDE; x++) {
        if (canvas_pixel_at(canvas, x, y) != BACKGROUND) {
            return x;
        }
    }
    return -1;
}

static void a_line_takes_the_whole_words_that_fit(void)
{
    CHECK_EQUAL(5, label_wrap_line(&TINY_FONT, "AB AB", 16));
    CHECK_EQUAL(2, label_wrap_line(&TINY_FONT, "AB AB", 15));
    CHECK_EQUAL(5, label_wrap_line(&TINY_FONT, "AB AB AB", 24));
    CHECK_EQUAL(8, label_wrap_line(&TINY_FONT, "AB AB AB", 25));
}

static void a_word_wider_than_the_line_gets_a_line_of_its_own(void)
{
    CHECK_EQUAL(4, label_wrap_line(&TINY_FONT, "ABAB AB", 8));
    CHECK_EQUAL(4, label_wrap_line(&TINY_FONT, "ABAB", 3));
    CHECK_EQUAL(3, label_wrap_count(&TINY_FONT, "B ABAB B", 8));
}

static void an_empty_text_is_one_empty_line(void)
{
    CHECK_EQUAL(0, label_wrap_line(&TINY_FONT, "", 10));
    CHECK_EQUAL(1, label_wrap_count(&TINY_FONT, "", 10));
}

static void lines_are_counted_by_the_words_that_fit_each(void)
{
    CHECK_EQUAL(1, label_wrap_count(&TINY_FONT, "AB AB", 16));
    CHECK_EQUAL(2, label_wrap_count(&TINY_FONT, "AB AB", 15));
    CHECK_EQUAL(3, label_wrap_count(&TINY_FONT, "AB AB AB", 7));
    CHECK_EQUAL(2, label_wrap_count(&TINY_FONT, "AB   AB", 7));
}

static void a_character_of_several_bytes_is_never_split_by_a_line_break(void)
{
    CHECK_EQUAL(3, label_wrap_line(&TINY_FONT,
                                   "\xC3\xA9"
                                   "A B",
                                   6));
    CHECK_EQUAL(2, label_wrap_count(&TINY_FONT,
                                    "\xC3\xA9"
                                    "A B",
                                    6));
}

static void a_line_break_in_the_text_ends_the_line(void)
{
    CHECK_EQUAL(2, label_wrap_line(&TINY_FONT, "AB\nAB", 100));
    CHECK_EQUAL(2, label_wrap_count(&TINY_FONT, "AB\nAB", 100));
    CHECK_EQUAL(3, label_wrap_count(&TINY_FONT, "AB AB\nB", 8));
    CHECK_EQUAL(3, label_wrap_count(&TINY_FONT, "A\n\nB", 100));
    Canvas canvas = wide_canvas(0);
    label_draw_wrapped(&canvas, INK_STYLE, (Label){.area = {0, 0, WIDE, TALL}, .text = "AB\nB"});
    CHECK_EQUAL(2, inked_lines(&canvas));
}

static void a_paragraph_starts_at_the_top_left_of_its_area(void)
{
    Canvas canvas = wide_canvas(0);
    label_draw_paragraph(&canvas, INK_STYLE, (Label){.area = {2, 0, 8, TALL}, .text = "AB AB"});
    CHECK_EQUAL(2, inked_lines(&canvas));
    CHECK(row_has_ink(&canvas, 0) || row_has_ink(&canvas, 1));
    CHECK_EQUAL(2 + 2, first_ink(&canvas, 1));
    CHECK_EQUAL(2 + 2, first_ink(&canvas, 4));
    CHECK(!row_has_ink(&canvas, 6));
}

static void a_paragraph_that_fits_one_line_is_drawn_like_a_label(void)
{
    Canvas paragraph = wide_canvas(0);
    Canvas label = wide_canvas(1);
    Rect area = {1, 3, 22, LINE};
    label_draw_paragraph(&paragraph, INK_STYLE, (Label){.area = area, .text = "AB B"});
    label_draw(&label, INK_STYLE, (Label){.area = area, .text = "AB B"});
    CHECK(memcmp(wide_pixels[0], wide_pixels[1], sizeof wide_pixels[0]) == 0);
    CHECK_EQUAL(1, inked_lines(&paragraph));
}

static void a_wrapped_text_that_fits_one_line_is_drawn_like_a_centred_label(void)
{
    Canvas wrapped = wide_canvas(0);
    Canvas centred = wide_canvas(1);
    Rect area = {1, 0, 22, TALL};
    label_draw_wrapped(&wrapped, INK_STYLE, (Label){.area = area, .text = "AB B"});
    label_draw(&centred, INK_STYLE,
               (Label){.area = label_centered_area(&TINY_FONT, area, "AB B"), .text = "AB B"});
    for (int index = 0; index < WIDE * TALL; index++) {
        CHECK_EQUAL(wide_pixels[1][index], wide_pixels[0][index]);
    }
    CHECK_EQUAL(1, inked_lines(&wrapped));
}

static void a_text_too_wide_for_one_line_is_drawn_on_several(void)
{
    Canvas canvas = wide_canvas(0);
    label_draw_wrapped(&canvas, INK_STYLE, (Label){.area = {0, 0, 8, TALL}, .text = "AB AB AB"});
    CHECK_EQUAL(3, inked_lines(&canvas));
    CHECK(!row_has_ink(&canvas, 0));
    CHECK(row_has_ink(&canvas, 1) || row_has_ink(&canvas, 2));
    CHECK(!row_has_ink(&canvas, 11));
}

static void every_wrapped_line_is_centred(void)
{
    Canvas canvas = wide_canvas(0);
    label_draw_wrapped(&canvas, INK_STYLE, (Label){.area = {0, 0, 12, TALL}, .text = "AB AB B"});
    CHECK_EQUAL(2, inked_lines(&canvas));
    CHECK_EQUAL(2 + 2, first_ink(&canvas, 4));
    CHECK_EQUAL(0 + 2, first_ink(&canvas, 7));
}

static void lines_that_do_not_fit_the_area_end_in_an_ellipsis_on_the_last_line(void)
{
    Canvas canvas = wide_canvas(0);
    Canvas whole = wide_canvas(1);
    label_draw_wrapped(&canvas, INK_STYLE,
                       (Label){.area = {0, 0, 8, 2 * LINE}, .text = "AB AB AB"});
    label_draw_wrapped(&whole, INK_STYLE, (Label){.area = {0, 0, 8, 2 * LINE}, .text = "AB AB"});
    CHECK_EQUAL(2, inked_lines(&canvas));
    CHECK(!row_has_ink(&canvas, 2 * LINE));
    CHECK(memcmp(wide_pixels[0], wide_pixels[1], LINE * WIDE * sizeof(Color)) == 0);
    CHECK(memcmp(wide_pixels[0] + LINE * WIDE, wide_pixels[1] + LINE * WIDE,
                 LINE * WIDE * sizeof(Color)) != 0);
}

static void an_area_lower_than_a_line_still_shows_one_line(void)
{
    Canvas canvas = wide_canvas(0);
    label_draw_wrapped(&canvas, INK_STYLE, (Label){.area = {0, 0, 8, 2}, .text = "AB AB"});
    CHECK_EQUAL(1, inked_lines(&canvas));
}

static void a_wrapped_text_never_paints_outside_its_area(void)
{
    Canvas canvas = wide_canvas(0);
    Rect area = {5, 3, 9, 6};
    label_draw_wrapped(&canvas, INK_STYLE, (Label){.area = area, .text = "ABABAB AB AB AB"});
    for (int y = 0; y < TALL; y++) {
        for (int x = 0; x < WIDE; x++) {
            bool inside = x >= 5 && x < 14 && y >= 3 && y < 9;
            CHECK(inside || canvas_pixel_at(&canvas, x, y) == BACKGROUND);
        }
    }
    CHECK_EQUAL(2, inked_lines(&canvas));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(text_that_fits_is_kept_whole),
        TEST_CASE(text_too_wide_is_cut_to_make_room_for_the_ellipsis),
        TEST_CASE(the_cut_never_splits_a_character),
        TEST_CASE(a_width_too_small_for_any_character_keeps_only_the_ellipsis),
        TEST_CASE(the_baseline_centres_the_line_in_its_area),
        TEST_CASE(a_label_never_paints_outside_its_area),
        TEST_CASE(a_scrolling_label_shifts_its_text_and_repeats_it),
        TEST_CASE(a_centred_area_sits_in_the_middle_of_its_bounds),
        TEST_CASE(a_centred_area_is_never_wider_than_its_bounds),
        TEST_CASE(a_line_takes_the_whole_words_that_fit),
        TEST_CASE(a_word_wider_than_the_line_gets_a_line_of_its_own),
        TEST_CASE(an_empty_text_is_one_empty_line),
        TEST_CASE(lines_are_counted_by_the_words_that_fit_each),
        TEST_CASE(a_character_of_several_bytes_is_never_split_by_a_line_break),
        TEST_CASE(a_line_break_in_the_text_ends_the_line),
        TEST_CASE(a_paragraph_starts_at_the_top_left_of_its_area),
        TEST_CASE(a_paragraph_that_fits_one_line_is_drawn_like_a_label),
        TEST_CASE(a_wrapped_text_that_fits_one_line_is_drawn_like_a_centred_label),
        TEST_CASE(a_text_too_wide_for_one_line_is_drawn_on_several),
        TEST_CASE(every_wrapped_line_is_centred),
        TEST_CASE(lines_that_do_not_fit_the_area_end_in_an_ellipsis_on_the_last_line),
        TEST_CASE(an_area_lower_than_a_line_still_shows_one_line),
        TEST_CASE(a_wrapped_text_never_paints_outside_its_area),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
