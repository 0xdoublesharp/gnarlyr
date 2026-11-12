#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "image/cover_grid.h"
#include "image/cover_image.h"

enum {
    FULL = COVER_GRID_SIZE,
    HALF = COVER_GRID_HALF
};

static uint16_t full[COVER_GRID_CELLS];
static uint16_t half[COVER_GRID_HALF_CELLS];

static bool read_bytes(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const uint8_t *bytes = context;
    memcpy(buffer, bytes + offset, length);
    return true;
}

static bool refuse_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    (void)offset;
    (void)buffer;
    (void)length;
    return false;
}

static void reader_end_requires_both_an_empty_buffer_and_exhausted_source(void)
{
    static uint8_t bytes[] = {1, 2, 3};
    uint8_t memory[IMAGE_READER_BUFFER];
    ImageArena arena = image_arena_make(memory, sizeof memory);
    ImageReader reader;
    CHECK(image_reader_init(&reader, (ImageSource){read_bytes, bytes, 0, sizeof bytes}, &arena));
    CHECK(!image_reader_at_end(&reader));
    CHECK(image_reader_available(&reader, sizeof bytes));
    CHECK(!image_reader_at_end(&reader));
    CHECK_EQUAL(1, image_reader_byte(&reader));
    CHECK(image_reader_skip(&reader, 2));
    CHECK(image_reader_at_end(&reader));

    arena = image_arena_make(memory, sizeof memory);
    CHECK(image_reader_init(&reader, (ImageSource){refuse_read, NULL, 0, 1}, &arena));
    CHECK(!image_reader_available(&reader, 1));
}

typedef struct JpegBytes {
    const uint8_t *bytes;
    uint32_t size;
} JpegBytes;

static bool read_jpeg(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const JpegBytes *jpeg = context;
    if (offset > jpeg->size || length > jpeg->size - offset) {
        return false;
    }
    memcpy(buffer, jpeg->bytes + offset, length);
    return true;
}

static void expect_jpeg_color(const uint8_t *bytes, uint32_t size, CoverColor color)
{
    static uint8_t memory[384 * 1024];
    static CoverGrid grid;
    ImageArena arena = image_arena_make(memory, sizeof memory);
    JpegBytes jpeg = {bytes, size};
    ImageReader reader;
    CHECK(image_reader_init(&reader, (ImageSource){read_jpeg, &jpeg, 0, size}, &arena));
    CoverImage image = {.grid = &grid};
    CHECK_EQUAL(COVER_IMAGE_CORRUPT, jpeg_decode(&reader, &arena, &image));
    CHECK_EQUAL(color, image.color);
}

static void jpeg_color_is_classified_from_frame_ids_and_adobe_metadata(void)
{
    static const uint8_t NO_FRAME[] = {0xFF, 0xD8, 0xFF, 0xD9};
    static const uint8_t GRAY[] = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
                                   0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xD9};
    static const uint8_t YCBCR[] = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00,
                                    0x01, 0x00, 0x01, 0x03, 0x01, 0x11, 0x00, 0x02,
                                    0x11, 0x00, 0x03, 0x11, 0x00, 0xFF, 0xD9};
    static const uint8_t RGB[] = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00,
                                  0x01, 0x00, 0x01, 0x03, 'R',  0x11, 0x00, 'G',
                                  0x11, 0x00, 'B',  0x11, 0x00, 0xFF, 0xD9};
    static const uint8_t ADOBE_RGB[] = {0xFF, 0xD8, 0xFF, 0xEE, 0x00, 0x0E, 'A',  'd',  'o',  'b',
                                        'e',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC0,
                                        0x00, 0x11, 0x08, 0x00, 0x01, 0x00, 0x01, 0x03, 0x01, 0x11,
                                        0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00, 0xFF, 0xD9};
    expect_jpeg_color(NO_FRAME, sizeof NO_FRAME, COVER_COLOR_GRAY);
    expect_jpeg_color(GRAY, sizeof GRAY, COVER_COLOR_GRAY);
    expect_jpeg_color(YCBCR, sizeof YCBCR, COVER_COLOR_YCBCR);
    expect_jpeg_color(RGB, sizeof RGB, COVER_COLOR_RGB);
    expect_jpeg_color(ADOBE_RGB, sizeof ADOBE_RGB, COVER_COLOR_RGB);
}

static uint16_t rgb565(uint32_t red, uint32_t green, uint32_t blue)
{
    return (uint16_t)((red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3));
}

static CoverGrid *new_grid(uint32_t width, uint32_t height)
{
    CoverGrid *grid = malloc(sizeof *grid);
    CHECK(cover_grid_init(grid, width, height));
    return grid;
}

static void add_pixel(CoverGrid *grid, uint32_t x, uint32_t y, int32_t red, int32_t green,
                      int32_t blue)
{
    int32_t values[3] = {red, green, blue};
    cover_grid_add_color(grid, (CoverRect){x, y, 1, 1}, values, 3);
}

static void a_uniform_image_renders_uniform_covers(void)
{
    CoverGrid *grid = new_grid(224, 224);
    for (uint32_t y = 0; y < 224; y++) {
        for (uint32_t x = 0; x < 224; x++) {
            add_pixel(grid, x, y, 100, 150, 200);
        }
    }
    cover_grid_render(grid, COVER_COLOR_RGB, full, half);
    CHECK_EQUAL(rgb565(100, 150, 200), full[0]);
    CHECK_EQUAL(rgb565(100, 150, 200), full[FULL * FULL - 1]);
    CHECK_EQUAL(rgb565(100, 150, 200), half[HALF * 20 + 33]);
    free(grid);
}

static void the_crop_keeps_the_centered_square(void)
{
    CoverGrid *grid = new_grid(336, 224);
    for (uint32_t y = 0; y < 224; y++) {
        for (uint32_t x = 0; x < 336; x++) {
            bool inside = x >= 56 && x < 280;
            bool left = x < 168;
            add_pixel(grid, x, y, inside && left ? 255 : 0, 0, inside && !left ? 255 : 0);
        }
    }
    cover_grid_render(grid, COVER_COLOR_RGB, full, half);
    CHECK_EQUAL(rgb565(255, 0, 0), full[0]);
    CHECK_EQUAL(rgb565(255, 0, 0), full[55]);
    CHECK_EQUAL(rgb565(0, 0, 255), full[56]);
    CHECK_EQUAL(rgb565(0, 0, 255), full[FULL * 111 + 111]);
    CHECK_EQUAL(rgb565(255, 0, 0), half[27]);
    CHECK_EQUAL(rgb565(0, 0, 255), half[28]);
    free(grid);
}

static void three_pixels_per_cell_average_exactly(void)
{
    CoverGrid *grid = new_grid(336, 336);
    for (uint32_t y = 0; y < 336; y++) {
        for (uint32_t x = 0; x < 336; x++) {
            add_pixel(grid, x, y, (int32_t)(x * 3 % 256), (int32_t)(y % 7 * 30), 0);
        }
    }
    cover_grid_render(grid, COVER_COLOR_RGB, full, half);
    for (uint32_t cell = 0; cell < FULL; cell++) {
        uint32_t red = 0;
        for (uint32_t x = cell * 3; x < cell * 3 + 3; x++) {
            red += x * 3 % 256;
        }
        uint32_t expected = (red * 2 + 3) / 6;
        uint32_t green_sum = 0;
        for (uint32_t y = 0; y < 3; y++) {
            green_sum += y % 7 * 30;
        }
        CHECK_EQUAL(rgb565(expected, (green_sum * 2 + 3) / 6, 0), full[cell]);
    }
    free(grid);
}

static double reference_cell(uint32_t span, uint32_t cell, const uint8_t *row)
{
    double low = (double)cell * span / (double)FULL;
    double high = (double)(cell + 1) * span / (double)FULL;
    double sum = 0.0;
    for (uint32_t x = (uint32_t)low; x < span && x < high; x++) {
        double from = x > low ? x : low;
        double to = x + 1.0 < high ? x + 1.0 : high;
        sum += row[x] * (to - from);
    }
    return sum / (high - low);
}

static void an_uneven_ratio_matches_an_exact_area_average(void)
{
    enum {
        SPAN = 250
    };
    static uint8_t row[SPAN];
    for (uint32_t x = 0; x < SPAN; x++) {
        row[x] = (uint8_t)(x * 37 % 256);
    }
    CoverGrid *grid = new_grid(SPAN, SPAN);
    for (uint32_t y = 0; y < SPAN; y++) {
        for (uint32_t x = 0; x < SPAN; x++) {
            add_pixel(grid, x, y, row[x], row[x], row[x]);
        }
    }
    cover_grid_render(grid, COVER_COLOR_GRAY, full, half);
    uint32_t off = 0;
    for (uint32_t cell = 0; cell < FULL; cell++) {
        double expected = reference_cell(SPAN, cell, row);
        int32_t got = (full[FULL * 50 + cell] >> 5 & 0x3F) * 255 / 63;
        off += got - expected > 6 || expected - got > 6;
    }
    CHECK_EQUAL(0, off);
    free(grid);
}

static void a_small_image_is_scaled_up_by_repeating_pixels(void)
{
    CoverGrid *grid = new_grid(56, 56);
    for (uint32_t y = 0; y < 56; y++) {
        for (uint32_t x = 0; x < 56; x++) {
            add_pixel(grid, x, y, (int32_t)(x * 4), (int32_t)(y * 4), 0);
        }
    }
    cover_grid_render(grid, COVER_COLOR_RGB, full, half);
    CHECK_EQUAL(rgb565(40, 80, 0), full[FULL * 40 + 20]);
    CHECK_EQUAL(rgb565(40, 80, 0), full[FULL * 41 + 21]);
    CHECK_EQUAL(rgb565(40, 80, 0), half[HALF * 20 + 10]);
    free(grid);
}

static void half_resolution_chroma_converts_to_rgb(void)
{
    CoverGrid *grid = new_grid(64, 64);
    cover_grid_set_sampling(grid, 1, 2, 2);
    cover_grid_set_sampling(grid, 2, 2, 2);
    for (uint32_t y = 0; y < 64; y++) {
        for (uint32_t x = 0; x < 64; x++) {
            cover_grid_add(grid, 0, (CoverRect){x, y, 1, 1}, 76);
        }
    }
    for (uint32_t y = 0; y < 32; y++) {
        for (uint32_t x = 0; x < 32; x++) {
            cover_grid_add(grid, 1, (CoverRect){2 * x, 2 * y, 2, 2}, 85);
            cover_grid_add(grid, 2, (CoverRect){2 * x, 2 * y, 2, 2}, 255);
        }
    }
    cover_grid_render(grid, COVER_COLOR_YCBCR, full, half);
    uint16_t red = full[FULL * 60 + 70];
    CHECK((red >> 11) >= 30);
    CHECK((red >> 5 & 0x3F) <= 2);
    CHECK((red & 0x1F) <= 1);
    CHECK_EQUAL(red, half[HALF * 30 + 35]);
    free(grid);
}

static void rectangles_outside_the_crop_do_not_touch_it(void)
{
    CoverGrid *grid = new_grid(300, 200);
    CHECK(!cover_grid_touches(grid, (CoverRect){0, 0, 50, 200}));
    CHECK(cover_grid_touches(grid, (CoverRect){0, 0, 51, 1}));
    CHECK(!cover_grid_touches(grid, (CoverRect){250, 0, 8, 8}));
    CHECK(cover_grid_touches(grid, (CoverRect){249, 199, 8, 8}));
    CHECK(!cover_grid_init(grid, 0, 10));
    CHECK(!cover_grid_init(grid, 20000, 10));
    free(grid);
}

static CoverGrid *quadrant_grid(void)
{
    CoverGrid *grid = new_grid(112, 112);
    for (uint32_t y = 0; y < 112; y++) {
        for (uint32_t x = 0; x < 112; x++) {
            add_pixel(grid, x, y, x < 56 && y < 56 ? 255 : 0, x >= 56 && y < 56 ? 255 : 0,
                      y >= 56 && x < 56 ? 255 : 0);
        }
    }
    return grid;
}

static void exif_orientation_six_turns_the_cover_clockwise(void)
{
    CoverGrid *grid = quadrant_grid();
    grid->orientation = 6;
    cover_grid_render(grid, COVER_COLOR_RGB, full, half);
    CHECK_EQUAL(rgb565(0, 0, 255), full[0]);
    CHECK_EQUAL(rgb565(255, 0, 0), full[FULL - 1]);
    CHECK_EQUAL(rgb565(0, 255, 0), full[FULL * FULL - 1]);
    CHECK_EQUAL(rgb565(0, 0, 0), full[FULL * (FULL - 1)]);
    CHECK_EQUAL(rgb565(255, 0, 0), half[HALF - 1]);
    free(grid);
}

static void every_exif_orientation_puts_the_first_quadrant_where_pillow_does(void)
{
    static const uint32_t corners[9] = {
        0, 0,        FULL - 1,        FULL * FULL - 1,  FULL * (FULL - 1),
        0, FULL - 1, FULL * FULL - 1, FULL * (FULL - 1)};
    for (uint32_t orientation = 1; orientation <= 8; orientation++) {
        CoverGrid *grid = quadrant_grid();
        grid->orientation = orientation;
        cover_grid_render(grid, COVER_COLOR_RGB, full, half);
        CHECK_EQUAL(rgb565(255, 0, 0), full[corners[orientation]]);
        free(grid);
    }
}

static void fill_inks(CoverGrid *grid, int32_t *black, const int32_t *inks)
{
    CHECK(cover_grid_attach_black(grid, black));
    for (uint32_t channel = 0; channel < 4; channel++) {
        cover_grid_add(grid, channel, (CoverRect){0, 0, 112, 112}, inks[channel]);
    }
}

typedef struct Lattice {
    uint32_t width;
    uint32_t height;
    uint32_t sample;
    uint32_t outputs;
} Lattice;

static void add_lattice(CoverGrid *grid, const Lattice *lattice, uint32_t seed)
{
    uint32_t block = lattice->sample * lattice->outputs;
    int32_t samples[COVER_GRID_BLOCK_OUTPUTS * COVER_GRID_BLOCK_OUTPUTS];
    for (uint32_t y = 0; y * block < lattice->height; y++) {
        for (uint32_t x = 0; x * block < lattice->width; x++) {
            CoverRect rect = {x * block, y * block, block, block};
            for (uint32_t index = 0; index < lattice->outputs * lattice->outputs; index++) {
                seed = seed * 1664525u + 1013904223u;
                samples[index] = (int32_t)(seed >> 20) - 2048;
            }
            if (lattice->outputs == 1) {
                cover_grid_add(grid, 0, rect, samples[0]);
            } else {
                cover_grid_add_block(grid, 0, rect, samples, lattice->outputs);
            }
        }
    }
}

static void sampled_blocks_sum_the_same_through_the_column_table(void)
{
    static const Lattice LATTICES[] = {
        {1500, 1000, 8, 1},  {640, 480, 2, 4},   {900, 901, 4, 2}, {333, 777, 2, 4},
        {2400, 2400, 16, 1}, {1000, 1000, 4, 2}, {500, 500, 2, 4}, {4001, 3999, 32, 1},
    };
    static CoverGrid table;
    static CoverGrid plain;
    for (uint32_t index = 0; index < sizeof LATTICES / sizeof LATTICES[0]; index++) {
        const Lattice *lattice = &LATTICES[index];
        CHECK(cover_grid_init(&table, lattice->width, lattice->height));
        CHECK(cover_grid_init(&plain, lattice->width, lattice->height));
        cover_grid_set_sampling(&table, 0, lattice->sample, lattice->sample);
        add_lattice(&table, lattice, index + 1);
        add_lattice(&plain, lattice, index + 1);
        CHECK(memcmp(table.sums[0], plain.sums[0], sizeof table.sums[0]) == 0);
    }
}

static void adobe_cmyk_multiplies_the_inverted_inks(void)
{
    static int32_t black[COVER_GRID_CELLS];
    CoverGrid *grid = new_grid(112, 112);
    fill_inks(grid, black, (const int32_t[]){255, 128, 0, 128});
    cover_grid_render(grid, COVER_COLOR_CMYK_INVERTED, full, half);
    CHECK_EQUAL(rgb565(128, 64, 0), full[FULL * 7 + 9]);
    CHECK_EQUAL(rgb565(128, 64, 0), half[HALF * 3 + 2]);
    cover_grid_init(grid, 112, 112);
    fill_inks(grid, black, (const int32_t[]){0, 127, 255, 127});
    cover_grid_render(grid, COVER_COLOR_CMYK, full, half);
    CHECK_EQUAL(rgb565(128, 64, 0), full[0]);
    free(grid);
}

static void ycck_inverts_the_converted_colour_before_black(void)
{
    static int32_t black[COVER_GRID_CELLS];
    CoverGrid *grid = new_grid(112, 112);
    fill_inks(grid, black, (const int32_t[]){0, 128, 128, 255});
    cover_grid_render(grid, COVER_COLOR_YCCK, full, half);
    CHECK_EQUAL(rgb565(255, 255, 255), full[100]);
    free(grid);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(reader_end_requires_both_an_empty_buffer_and_exhausted_source),
        TEST_CASE(jpeg_color_is_classified_from_frame_ids_and_adobe_metadata),
        TEST_CASE(a_uniform_image_renders_uniform_covers),
        TEST_CASE(the_crop_keeps_the_centered_square),
        TEST_CASE(three_pixels_per_cell_average_exactly),
        TEST_CASE(an_uneven_ratio_matches_an_exact_area_average),
        TEST_CASE(a_small_image_is_scaled_up_by_repeating_pixels),
        TEST_CASE(half_resolution_chroma_converts_to_rgb),
        TEST_CASE(rectangles_outside_the_crop_do_not_touch_it),
        TEST_CASE(exif_orientation_six_turns_the_cover_clockwise),
        TEST_CASE(every_exif_orientation_puts_the_first_quadrant_where_pillow_does),
        TEST_CASE(sampled_blocks_sum_the_same_through_the_column_table),
        TEST_CASE(adobe_cmyk_multiplies_the_inverted_inks),
        TEST_CASE(ycck_inverts_the_converted_colour_before_black),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
