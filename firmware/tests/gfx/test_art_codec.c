#include "check.h"
#include "gfx/art_codec.h"

enum {
    WIDTH = 8,
    CANARY = 0xBEEF
};

static ArtPalette palette;

static void reset_palette(void)
{
    palette = (ArtPalette){.alpha_entries = 0, .entries = ART_PALETTE_MAX};
    for (int index = 0; index < ART_PALETTE_MAX; index++) {
        palette.colors[index] = (Color)(0x0101 * (unsigned)index);
    }
}

static ArtRow row_of(const uint8_t *bytes, int count, int width)
{
    return (ArtRow){bytes, bytes + count, width};
}

static void fill(Color *pixels, int count, Color color)
{
    for (int at = 0; at < count; at++) {
        pixels[at] = color;
    }
}

static void an_indexed_literal_packet_writes_its_palette_colours(void)
{
    reset_palette();
    static const uint8_t bytes[] = {0x03, 1, 2, 3, 4};
    Color out[4];
    ArtRow row = row_of(bytes, sizeof bytes, 4);
    CHECK(art_row_indexed(&row, &palette, (ArtSpan){out, 0, 4}));
    CHECK_EQUAL(0x0101, out[0]);
    CHECK_EQUAL(0x0404, out[3]);
    CHECK(row.at == bytes + sizeof bytes);
}

static void a_row_with_bytes_left_after_its_pixels_is_refused(void)
{
    reset_palette();
    static const uint8_t indexed[] = {0x83, 1, 0x00};
    static const uint8_t lossless[] = {0x03, 0x00};
    Color out[4];
    ArtRow row = row_of(indexed, sizeof indexed, 4);
    CHECK(!art_row_indexed(&row, &palette, (ArtSpan){out, 0, 4}));
    row = row_of(indexed, 2, 4);
    CHECK(art_row_indexed(&row, &palette, (ArtSpan){out, 0, 4}));
    row = row_of(lossless, sizeof lossless, 4);
    CHECK(!art_row_lossless(&row, false, (ArtSpan){out, 0, 4}));
    row = row_of(lossless, 1, 4);
    CHECK(art_row_lossless(&row, false, (ArtSpan){out, 0, 4}));
}

static void an_indexed_run_packet_repeats_one_colour(void)
{
    reset_palette();
    static const uint8_t bytes[] = {0x87, 9};
    Color out[WIDTH];
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_indexed(&row, &palette, (ArtSpan){out, 0, WIDTH}));
    for (int at = 0; at < WIDTH; at++) {
        CHECK_EQUAL(0x0909, out[at]);
    }
}

static void an_indexed_row_writes_only_its_span(void)
{
    reset_palette();
    static const uint8_t bytes[] = {0x82, 1, 0x04, 2, 3, 4, 5, 6};
    Color out[WIDTH + 2];
    fill(out, WIDTH + 2, CANARY);
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_indexed(&row, &palette, (ArtSpan){out + 1, 2, 6}));
    CHECK_EQUAL(CANARY, out[0]);
    CHECK_EQUAL(0x0101, out[1]);
    CHECK_EQUAL(0x0202, out[2]);
    CHECK_EQUAL(0x0404, out[4]);
    CHECK_EQUAL(CANARY, out[5]);
    CHECK(row.at == bytes + sizeof bytes);
}

static void transparent_indexed_entries_leave_the_canvas_untouched(void)
{
    reset_palette();
    palette.alpha_entries = 2;
    palette.alpha[0] = 0x00;
    static const uint8_t bytes[] = {0x83, 0, 0x83, 1};
    Color out[WIDTH];
    fill(out, WIDTH, CANARY);
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_indexed(&row, &palette, (ArtSpan){out, 0, WIDTH}));
    CHECK_EQUAL(CANARY, out[0]);
    CHECK_EQUAL(CANARY, out[7]);
}

static void partial_alpha_indexed_entries_blend_over_the_canvas(void)
{
    reset_palette();
    palette.alpha_entries = 2;
    palette.alpha[0] = 0x8F;
    palette.colors[1] = 0xFFFF;
    static const uint8_t bytes[] = {0x01, 0, 1, 0x81, 1, 0x81, 2};
    Color out[6];
    fill(out, 6, 0x0000);
    ArtRow row = row_of(bytes, sizeof bytes, 6);
    CHECK(art_row_indexed(&row, &palette, (ArtSpan){out, 0, 6}));
    CHECK_EQUAL(palette.colors[0], out[0]);
    CHECK_EQUAL(color_blend(0x0000, 0xFFFF, 8), out[1]);
    CHECK_EQUAL(color_blend(0x0000, 0xFFFF, 8), out[3]);
    CHECK_EQUAL(0x0202, out[5]);
}

static void an_indexed_row_that_ends_early_or_overruns_its_width_fails(void)
{
    reset_palette();
    static const uint8_t short_row[] = {0x82, 1};
    static const uint8_t long_row[] = {0x88, 1};
    Color out[WIDTH];
    ArtRow row = row_of(short_row, sizeof short_row, WIDTH);
    CHECK(!art_row_indexed(&row, &palette, (ArtSpan){out, 0, WIDTH}));
    row = row_of(long_row, sizeof long_row, WIDTH);
    CHECK(!art_row_indexed(&row, &palette, (ArtSpan){out, 0, WIDTH}));
}

static void a_truncated_indexed_packet_fails(void)
{
    reset_palette();
    static const uint8_t literal[] = {0x07, 1, 2, 3};
    static const uint8_t run[] = {0x87};
    Color out[WIDTH];
    ArtRow row = row_of(literal, sizeof literal, WIDTH);
    CHECK(!art_row_indexed(&row, &palette, (ArtSpan){out, 0, WIDTH}));
    row = row_of(run, sizeof run, WIDTH);
    CHECK(!art_row_indexed(&row, &palette, (ArtSpan){out, 0, WIDTH}));
}

static void indices_at_or_beyond_the_palette_are_reported(void)
{
    static const uint8_t bytes[] = {0x01, 3, 4, 0x85, 2};
    CHECK(art_row_check(row_of(bytes, sizeof bytes, WIDTH), ART_CODEC_INDEXED, 5, false));
    CHECK(!art_row_check(row_of(bytes, sizeof bytes, WIDTH), ART_CODEC_INDEXED, 4, false));
    static const uint8_t run[] = {0x87, 7};
    CHECK(!art_row_check(row_of(run, sizeof run, WIDTH), ART_CODEC_INDEXED, 7, false));
}

static void lossless_rows_start_from_black_and_repeat_the_previous_colour(void)
{
    static const uint8_t bytes[] = {0x01, 0x80, 0x34, 0x12, 0x04};
    Color out[WIDTH];
    fill(out, WIDTH, CANARY);
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_lossless(&row, false, (ArtSpan){out, 0, WIDTH}));
    CHECK_EQUAL(0x0000, out[0]);
    CHECK_EQUAL(0x0000, out[1]);
    CHECK_EQUAL(0x1234, out[2]);
    CHECK_EQUAL(0x1234, out[7]);
}

static void lossless_deltas_step_each_channel_with_wraparound(void)
{
    static const uint8_t bytes[] = {0x41, 0xFF, 0x0A, 0x05};
    Color out[WIDTH];
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_lossless(&row, false, (ArtSpan){out, 0, WIDTH}));
    CHECK_EQUAL((Color)(4 << 11 | 7 << 5 | 4), out[0]);
    CHECK_EQUAL((Color)(63 << 5), out[1]);
    CHECK_EQUAL((Color)(63 << 5), out[7]);
}

static void lossless_literals_set_the_previous_colour(void)
{
    static const uint8_t bytes[] = {0x80, 0x00, 0xF8, 0x40, 0x1A, 0x05};
    Color out[WIDTH];
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_lossless(&row, false, (ArtSpan){out, 0, WIDTH}));
    CHECK_EQUAL(0xF800, out[0]);
    CHECK_EQUAL((Color)(27 << 11 | 57 << 5 | 28), out[1]);
    CHECK_EQUAL((Color)(27 << 11 | 57 << 5 | 28), out[7]);
}

static void lossless_skips_and_alpha_prefixes_need_an_alpha_asset(void)
{
    static const uint8_t bytes[] = {0xC2, 0xE8, 0x80, 0xFF, 0xFF, 0x03};
    Color out[WIDTH];
    fill(out, WIDTH, 0x0000);
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(!art_row_lossless(&row, false, (ArtSpan){out, 0, WIDTH}));
    row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_lossless(&row, true, (ArtSpan){out, 0, WIDTH}));
    CHECK_EQUAL(0x0000, out[2]);
    CHECK_EQUAL(color_blend(0x0000, 0xFFFF, 8), out[3]);
    CHECK_EQUAL(0xFFFF, out[4]);
    CHECK_EQUAL(0xFFFF, out[7]);
}

static void an_alpha_prefix_blends_exactly_the_next_packet(void)
{
    static const uint8_t bytes[] = {0xE4, 0x01, 0x03};
    Color out[6];
    fill(out, 6, 0xFFFF);
    ArtRow row = row_of(bytes, sizeof bytes, 6);
    CHECK(art_row_lossless(&row, true, (ArtSpan){out, 0, 6}));
    CHECK_EQUAL(color_blend(0xFFFF, 0x0000, 4), out[0]);
    CHECK_EQUAL(color_blend(0xFFFF, 0x0000, 4), out[1]);
    CHECK_EQUAL(0x0000, out[2]);
    CHECK_EQUAL(0x0000, out[5]);
    static const uint8_t before_skip[] = {0xE4, 0xC5};
    static const uint8_t twice[] = {0xE4, 0xE4, 0x05};
    static const uint8_t dangling[] = {0x04, 0xE4};
    const uint8_t *cases[] = {before_skip, twice, dangling};
    for (int at = 0; at < 3; at++) {
        row = row_of(cases[at], at == 0 ? 2 : (at == 1 ? 3 : 2), 6);
        CHECK(!art_row_lossless(&row, true, (ArtSpan){out, 0, 6}));
    }
}

static void lossless_rows_write_only_their_span(void)
{
    static const uint8_t bytes[] = {0x83, 1, 0, 2, 0, 3, 0, 4, 0, 0x43, 0x8A, 0x8A, 0x8A, 0x8A};
    Color full[WIDTH];
    Color part[WIDTH + 2];
    fill(part, WIDTH + 2, CANARY);
    ArtRow row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_lossless(&row, false, (ArtSpan){full, 0, WIDTH}));
    row = row_of(bytes, sizeof bytes, WIDTH);
    CHECK(art_row_lossless(&row, false, (ArtSpan){part + 1, 3, 6}));
    CHECK_EQUAL(CANARY, part[0]);
    CHECK_EQUAL(full[3], part[1]);
    CHECK_EQUAL(full[4], part[2]);
    CHECK_EQUAL(full[5], part[3]);
    CHECK_EQUAL(CANARY, part[4]);
    CHECK(row.at == bytes + sizeof bytes);
}

static void lossless_rejects_reserved_packets_and_short_input(void)
{
    Color out[WIDTH];
    static const uint8_t reserved[] = {0xE0, 0, 0, 0x06};
    static const uint8_t high[] = {0xEF, 0, 0, 0x06};
    static const uint8_t short_literal[] = {0x87, 1, 0};
    static const uint8_t short_alpha[] = {0xE5, 0x80, 1};
    static const uint8_t overrun[] = {0x08};
    const uint8_t *cases[] = {reserved, high, short_literal, short_alpha, overrun};
    const int sizes[] = {sizeof reserved, sizeof high, sizeof short_literal, sizeof short_alpha,
                         sizeof overrun};
    for (int at = 0; at < 5; at++) {
        ArtRow row = row_of(cases[at], sizes[at], WIDTH);
        CHECK(!art_row_lossless(&row, true, (ArtSpan){out, 0, WIDTH}));
    }
}

static void a_row_check_accepts_exactly_the_rows_the_decoders_finish(void)
{
    static const uint8_t indexed[] = {0x82, 1, 0x04, 2, 3, 4, 5, 6};
    CHECK(art_row_check(row_of(indexed, sizeof indexed, WIDTH), ART_CODEC_INDEXED, 7, false));
    CHECK(!art_row_check(row_of(indexed, sizeof indexed, WIDTH), ART_CODEC_INDEXED, 6, false));
    CHECK(!art_row_check(row_of(indexed, sizeof indexed - 1, WIDTH), ART_CODEC_INDEXED, 7, false));
    static const uint8_t trailing[] = {0x87, 1, 0x00};
    CHECK(!art_row_check(row_of(trailing, sizeof trailing, WIDTH), ART_CODEC_INDEXED, 7, false));
    static const uint8_t lossless[] = {0xC2, 0xE8, 0x80, 0xFF, 0xFF, 0x03};
    CHECK(art_row_check(row_of(lossless, sizeof lossless, WIDTH), ART_CODEC_LOSSLESS, 0, true));
    CHECK(!art_row_check(row_of(lossless, sizeof lossless, WIDTH), ART_CODEC_LOSSLESS, 0, false));
    static const uint8_t prefixed_skip[] = {0xE4, 0xC7};
    CHECK(!art_row_check(row_of(prefixed_skip, sizeof prefixed_skip, WIDTH), ART_CODEC_LOSSLESS, 0,
                         true));
    static const uint8_t extra[] = {0x07, 0x00};
    CHECK(!art_row_check(row_of(extra, sizeof extra, WIDTH), ART_CODEC_LOSSLESS, 0, false));
    CHECK(!art_row_check(row_of(extra, 1, WIDTH), 3, 0, false));
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(an_indexed_literal_packet_writes_its_palette_colours),
        TEST_CASE(a_row_with_bytes_left_after_its_pixels_is_refused),
        TEST_CASE(an_indexed_run_packet_repeats_one_colour),
        TEST_CASE(an_indexed_row_writes_only_its_span),
        TEST_CASE(transparent_indexed_entries_leave_the_canvas_untouched),
        TEST_CASE(partial_alpha_indexed_entries_blend_over_the_canvas),
        TEST_CASE(an_indexed_row_that_ends_early_or_overruns_its_width_fails),
        TEST_CASE(a_truncated_indexed_packet_fails),
        TEST_CASE(indices_at_or_beyond_the_palette_are_reported),
        TEST_CASE(lossless_rows_start_from_black_and_repeat_the_previous_colour),
        TEST_CASE(lossless_deltas_step_each_channel_with_wraparound),
        TEST_CASE(lossless_literals_set_the_previous_colour),
        TEST_CASE(lossless_skips_and_alpha_prefixes_need_an_alpha_asset),
        TEST_CASE(an_alpha_prefix_blends_exactly_the_next_packet),
        TEST_CASE(lossless_rows_write_only_their_span),
        TEST_CASE(lossless_rejects_reserved_packets_and_short_input),
        TEST_CASE(a_row_check_accepts_exactly_the_rows_the_decoders_finish),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
