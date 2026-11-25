#include <string.h>

#include "check.h"
#include "fonts/fonts.h"
#include "gfx/text.h"
#include "ui/footer_hints.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    WIDE = 480,
    TALL = 222,
    PADDING = 12,
    LINE = 24,
    STACK = 48
};

static Color pixels[WIDE * WIDE];

static const FooterHint BACK_CANCEL[] = {{FOOTER_KEY_BACK, STR_CANCEL}};
static const FooterHint FOUR[] = {{FOOTER_KEY_VOL, STR_HINT_CHANGE},
                                  {FOOTER_KEY_PLAY, STR_HINT_OPEN},
                                  {FOOTER_KEY_BACK, STR_HINT_CLOSE},
                                  {FOOTER_KEY_HOLD_PLAY, STR_HINT_ALWAYS}};

static Theme footer_theme(bool portrait)
{
    Theme theme = TEST_THEME;
    theme.fonts = (ThemeFonts){&FONT_INTER_REGULAR_16, &FONT_INTER_REGULAR_16,
                               &FONT_INTER_REGULAR_16, &FONT_INTER_REGULAR_16};
    theme.metrics.screen_width = portrait ? TALL : WIDE;
    theme.metrics.screen_height = portrait ? WIDE : TALL;
    theme.metrics.padding = PADDING;
    return theme;
}

static Canvas blank(const Theme *theme)
{
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    canvas_clear(&canvas, 0x0821);
    return canvas;
}

static FooterHints hints_of(const FooterHint *items, int count, FooterAlign align)
{
    return (FooterHints){.items = items, .count = (uint8_t)count, .align = (uint8_t)align};
}

static int hint_width(const Theme *theme, const FooterHint *hint)
{
    const Font *font = theme->fonts.small;
    return text_width(font, footer_hints_key_name((FooterKey)hint->key)) + 2 * FOOTER_CHIP_PADDING +
           FOOTER_CHIP_GAP + text_width(font, i18n_text((StringId)hint->verb));
}

static int first_column_with(const Canvas *canvas, Rect area, Color color)
{
    for (int x = area.x; x < area.x + area.width; x++) {
        if (ui_test_area_has_color(canvas, (Rect){x, area.y, 1, area.height}, color)) {
            return x;
        }
    }
    return -1;
}

static int last_column_with(const Canvas *canvas, Rect area, Color color)
{
    for (int x = area.x + area.width - 1; x >= area.x; x--) {
        if (ui_test_area_has_color(canvas, (Rect){x, area.y, 1, area.height}, color)) {
            return x;
        }
    }
    return -1;
}

static int last_column_not(const Canvas *canvas, Rect area, Color color)
{
    for (int x = area.x + area.width - 1; x >= area.x; x--) {
        if (!ui_test_area_has_only(canvas, (Rect){x, area.y, 1, area.height}, color)) {
            return x;
        }
    }
    return -1;
}

static void each_key_chip_is_named_by_its_own_string(void)
{
    CHECK(strcmp("Vol", footer_hints_key_name(FOOTER_KEY_VOL)) == 0);
    CHECK(strcmp("Play", footer_hints_key_name(FOOTER_KEY_PLAY)) == 0);
    CHECK(strcmp("Menu", footer_hints_key_name(FOOTER_KEY_MENU)) == 0);
    CHECK(strcmp("Back", footer_hints_key_name(FOOTER_KEY_BACK)) == 0);
    CHECK(strcmp("Hold Play", footer_hints_key_name(FOOTER_KEY_HOLD_PLAY)) == 0);
    CHECK(strcmp("Hold Menu", footer_hints_key_name(FOOTER_KEY_HOLD_MENU)) == 0);
    CHECK(strcmp("Hold Back", footer_hints_key_name(FOOTER_KEY_HOLD_BACK)) == 0);
}

static void a_footer_without_hints_takes_no_room(void)
{
    Theme theme = footer_theme(false);
    CHECK_EQUAL(0, footer_hints_height(&theme, hints_of(NULL, 0, FOOTER_ALIGN_END)));
}

static void hints_that_fit_take_one_24_pixel_line(void)
{
    Theme theme = footer_theme(false);
    CHECK_EQUAL(LINE, footer_hints_height(&theme, hints_of(FOUR, 3, FOOTER_ALIGN_START)));
}

static void hints_wrap_between_whole_hints(void)
{
    Theme theme = footer_theme(false);
    FooterHints hints = hints_of(FOUR, 4, FOOTER_ALIGN_START);
    int one = hint_width(&theme, &FOUR[0]);
    CHECK_EQUAL(4, footer_hints_lines(&theme, hints, one));
    CHECK_EQUAL(1, footer_hints_lines(&theme, hints_of(FOUR, 1, FOOTER_ALIGN_START), one));
    int two = one + hint_width(&theme, &FOUR[1]) + 2 * FOOTER_HINT_GAP +
              text_width(theme.fonts.small, "\xC2\xB7");
    CHECK_EQUAL(2, footer_hints_lines(&theme, hints_of(FOUR, 2, FOOTER_ALIGN_START), two - 1));
    CHECK_EQUAL(1, footer_hints_lines(&theme, hints_of(FOUR, 2, FOOTER_ALIGN_START), two));
}

static void a_hint_wider_than_the_line_keeps_a_line_of_its_own(void)
{
    Theme theme = footer_theme(false);
    CHECK_EQUAL(4, footer_hints_lines(&theme, hints_of(FOUR, 4, FOOTER_ALIGN_START), 4));
    Canvas canvas = blank(&theme);
    Rect area = {0, TALL - 4 * LINE, 40, 4 * LINE};
    footer_hints_draw(&canvas, &theme, area, hints_of(FOUR, 4, FOOTER_ALIGN_START));
    Rect beyond = {40, 0, WIDE - 40, TALL};
    CHECK(!ui_test_area_has_color(&canvas, beyond, theme.colors.header_text));
}

static void portrait_stacks_each_key_over_its_verb(void)
{
    Theme theme = footer_theme(true);
    FooterHints hints = hints_of(BACK_CANCEL, 1, FOOTER_ALIGN_END);
    CHECK_EQUAL(STACK, footer_hints_height(&theme, hints));
    Canvas canvas = blank(&theme);
    Rect area = {0, WIDE - STACK, TALL, STACK};
    footer_hints_draw(&canvas, &theme, area, hints);
    Rect top = {0, area.y + 2, TALL, 20};
    Rect bottom = {0, area.y + 24, TALL, 20};
    CHECK(ui_test_area_has_color(&canvas, top, theme.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, bottom, theme.colors.header_text));
    int left = first_column_with(&canvas, top, theme.colors.header_text);
    int right = last_column_with(&canvas, top, theme.colors.header_text);
    CHECK(left > PADDING + 40);
    CHECK((left + right) / 2 >= TALL / 2 - 4 && (left + right) / 2 <= TALL / 2 + 4);
}

static void end_aligned_hints_end_at_the_padding(void)
{
    Theme theme = footer_theme(false);
    FooterHints hints = hints_of(BACK_CANCEL, 1, FOOTER_ALIGN_END);
    Canvas canvas = blank(&theme);
    Rect area = {0, TALL - LINE, WIDE, LINE};
    footer_hints_draw(&canvas, &theme, area, hints);
    Rect inner = {0, area.y + 1, WIDE, LINE - 1};
    Color verb =
        color_blend(theme.colors.header_background, theme.colors.header_text, FOOTER_VERB_ALPHA);
    int last = last_column_not(&canvas, inner, theme.colors.header_background);
    CHECK(last < WIDE - PADDING);
    CHECK(last >= WIDE - PADDING - 4);
    CHECK(first_column_with(&canvas, inner, verb) > WIDE / 2);
}

static void start_aligned_hints_begin_at_the_padding(void)
{
    Theme theme = footer_theme(false);
    Canvas canvas = blank(&theme);
    Rect area = {0, TALL - LINE, WIDE, LINE};
    footer_hints_draw(&canvas, &theme, area, hints_of(FOUR, 2, FOOTER_ALIGN_START));
    Color verb =
        color_blend(theme.colors.header_background, theme.colors.header_text, FOOTER_VERB_ALPHA);
    CHECK_EQUAL(PADDING, first_column_with(&canvas, (Rect){0, area.y + 1, WIDE, LINE - 1}, verb));
}

static void the_band_sits_on_the_header_colour_under_a_divider(void)
{
    Theme theme = footer_theme(false);
    Canvas canvas = blank(&theme);
    Rect area = {0, TALL - LINE, WIDE, LINE};
    footer_hints_draw(&canvas, &theme, area, hints_of(BACK_CANCEL, 1, FOOTER_ALIGN_END));
    CHECK(ui_test_area_has_only(&canvas, (Rect){0, area.y, WIDE, 1}, theme.colors.divider));
    CHECK(ui_test_area_has_only(&canvas, (Rect){0, area.y + 1, PADDING, LINE - 1},
                                theme.colors.header_background));
}

static void hints_follow_their_given_order(void)
{
    Theme theme = footer_theme(false);
    Canvas canvas = blank(&theme);
    static const FooterHint TWO[] = {{FOOTER_KEY_MENU, STR_HINT_OPEN},
                                     {FOOTER_KEY_HOLD_BACK, STR_HINT_CLOSE}};
    Rect area = {0, TALL - LINE, WIDE, LINE};
    footer_hints_draw(&canvas, &theme, area, hints_of(TWO, 2, FOOTER_ALIGN_START));
    int first = hint_width(&theme, &TWO[0]);
    int gap = 2 * FOOTER_HINT_GAP + text_width(theme.fonts.small, "\xC2\xB7");
    Rect after_first = {PADDING + first + gap, area.y + 1, 1, LINE - 1};
    Color verb =
        color_blend(theme.colors.header_background, theme.colors.header_text, FOOTER_VERB_ALPHA);
    CHECK(ui_test_area_has_color(&canvas, after_first, verb));
}

static void an_inline_cue_draws_on_the_screen_without_a_band(void)
{
    Theme theme = footer_theme(false);
    Canvas canvas = blank(&theme);
    Rect area = {20, 100, 200, 20};
    footer_hints_draw_inline(&canvas, &theme, area, hints_of(BACK_CANCEL, 1, FOOTER_ALIGN_START));
    CHECK(!ui_test_area_has_color(&canvas, area, theme.colors.header_background));
    CHECK(ui_test_area_has_color(&canvas, area, theme.colors.text));
    CHECK_EQUAL(20, first_column_with(&canvas, area, theme.colors.text_secondary));
}

static int service_calls;

static void count_service(void)
{
    service_calls++;
}

static void drawing_services_the_visible_hints_and_separator(void)
{
    Theme theme = footer_theme(false);
    Canvas canvas = blank(&theme);
    Rect area = {0, TALL - LINE, WIDE, LINE};
    FooterHints hints = hints_of(FOUR, 2, FOOTER_ALIGN_START);
    hints.service = count_service;
    service_calls = 0;
    Canvas clipped = canvas_clipped(&canvas, (Rect){0, area.y - 1, WIDE, 1});
    footer_hints_draw(&clipped, &theme, area, hints);
    CHECK_EQUAL(0, service_calls);
    clipped = canvas_clipped(&canvas, (Rect){PADDING, area.y + LINE / 2, 1, 1});
    footer_hints_draw(&clipped, &theme, area, hints);
    CHECK_EQUAL(3, service_calls);
    service_calls = 0;
    footer_hints_draw(&canvas, &theme, area, hints);
    CHECK_EQUAL(13, service_calls);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(each_key_chip_is_named_by_its_own_string),
        TEST_CASE(a_footer_without_hints_takes_no_room),
        TEST_CASE(hints_that_fit_take_one_24_pixel_line),
        TEST_CASE(hints_wrap_between_whole_hints),
        TEST_CASE(a_hint_wider_than_the_line_keeps_a_line_of_its_own),
        TEST_CASE(portrait_stacks_each_key_over_its_verb),
        TEST_CASE(end_aligned_hints_end_at_the_padding),
        TEST_CASE(start_aligned_hints_begin_at_the_padding),
        TEST_CASE(the_band_sits_on_the_header_colour_under_a_divider),
        TEST_CASE(hints_follow_their_given_order),
        TEST_CASE(an_inline_cue_draws_on_the_screen_without_a_band),
        TEST_CASE(drawing_services_the_visible_hints_and_separator),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
