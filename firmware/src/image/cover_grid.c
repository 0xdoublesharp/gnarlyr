#include "image/cover_grid.h"

#include <stddef.h>

enum {
    CELLS = COVER_GRID_SIZE,
    HALF = COVER_GRID_HALF,
    Q8_ONE = 256,
    BYTE_MAX = 255,
    CHROMA_CENTER_Q8 = 128 * Q8_ONE,
    CR_TO_R = 91881,
    CB_TO_G = 22554,
    CR_TO_G = 46802,
    CB_TO_B = 116130,
    COEFFICIENT_SHIFT = 16,
    INK_FULL_Q8 = BYTE_MAX * Q8_ONE,
    SWAP = 1,
    FLIP_X = 2,
    FLIP_Y = 4,
    ORIENTATION_COUNT = 9
};

static const uint8_t ORIENTATIONS[ORIENTATION_COUNT] = {0,
                                                        0,
                                                        FLIP_X,
                                                        FLIP_X | FLIP_Y,
                                                        FLIP_Y,
                                                        SWAP,
                                                        SWAP | FLIP_Y,
                                                        SWAP | FLIP_X | FLIP_Y,
                                                        SWAP | FLIP_X};

bool cover_grid_init(CoverGrid *grid, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > COVER_GRID_LIMIT || height > COVER_GRID_LIMIT) {
        return false;
    }
    grid->span = width < height ? width : height;
    grid->start_x = (width - grid->span) / 2;
    grid->start_y = (height - grid->span) / 2;
    grid->black = NULL;
    grid->orientation = 1;
    grid->cached_height = 0;
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        grid->sample_width[channel] = 1;
        grid->sample_height[channel] = 1;
        for (uint32_t cell = 0; cell < COVER_GRID_CELLS; cell++) {
            grid->sums[channel][cell] = 0;
        }
    }
    return true;
}

bool cover_grid_attach_black(CoverGrid *grid, int32_t *plane)
{
    if (plane == NULL) {
        return false;
    }
    for (uint32_t cell = 0; cell < COVER_GRID_CELLS; cell++) {
        plane[cell] = 0;
    }
    grid->black = plane;
    return true;
}

void cover_grid_set_sampling(CoverGrid *grid, uint32_t channel, uint32_t width, uint32_t height)
{
    grid->sample_width[channel] = width;
    grid->sample_height[channel] = height;
}

static void axis_span(uint32_t start, uint32_t span, uint32_t position, uint32_t length,
                      CoverSpan *out)
{
    uint32_t window_low = start * CELLS;
    uint32_t window_high = window_low + span * CELLS;
    uint32_t low = position * CELLS;
    uint32_t high = (position + length) * CELLS;
    low = low > window_low ? low : window_low;
    high = high < window_high ? high : window_high;
    out->count = 0;
    if (low >= high) {
        return;
    }
    out->first = (low - window_low) / span;
    uint32_t last = (high - window_low - 1) / span;
    for (uint32_t cell = out->first; cell <= last; cell++) {
        uint32_t cell_low = window_low + cell * span;
        uint32_t cell_high = cell_low + span;
        uint32_t from = low > cell_low ? low : cell_low;
        uint32_t to = high < cell_high ? high : cell_high;
        out->weights[out->count++] = (uint16_t)((to - from) * COVER_GRID_WEIGHT_ONE / span);
    }
}

bool cover_grid_touches(const CoverGrid *grid, CoverRect rect)
{
    uint32_t end_x = grid->start_x + grid->span;
    uint32_t end_y = grid->start_y + grid->span;
    return rect.x < end_x && rect.x + rect.width > grid->start_x && rect.y < end_y &&
           rect.y + rect.height > grid->start_y;
}

static void add_spans(CoverGrid *grid, uint32_t channel, const CoverSpan *x, const CoverSpan *y,
                      int32_t value)
{
    for (uint32_t row = 0; row < y->count; row++) {
        int32_t *plane = channel == COVER_GRID_BLACK ? grid->black : grid->sums[channel];
        int32_t *sums = plane + (y->first + row) * CELLS + x->first;
        int32_t row_value = value * (int32_t)y->weights[row];
        for (uint32_t column = 0; column < x->count; column++) {
            int32_t product = row_value * (int32_t)x->weights[column];
            sums[column] = (int32_t)((uint32_t)sums[column] + (uint32_t)product);
        }
    }
}

static const CoverSpan *rows_of(CoverGrid *grid, uint32_t y, uint32_t height)
{
    if (grid->cached_height != height || grid->cached_y != y) {
        axis_span(grid->start_y, grid->span, y, height, &grid->cached_rows);
        grid->cached_y = y;
        grid->cached_height = height;
    }
    return &grid->cached_rows;
}

void cover_grid_add(CoverGrid *grid, uint32_t channel, CoverRect rect, int32_t value)
{
    CoverSpan x;
    const CoverSpan *y = rows_of(grid, rect.y, rect.height);
    if (y->count == 0) {
        return;
    }
    axis_span(grid->start_x, grid->span, rect.x, rect.width, &x);
    add_spans(grid, channel, &x, y, value);
}

void cover_grid_add_block(CoverGrid *grid, uint32_t channel, CoverRect block,
                          const int32_t *samples, uint32_t outputs)
{
    CoverSpan across[COVER_GRID_BLOCK_OUTPUTS];
    uint32_t width = block.width / outputs;
    uint32_t height = block.height / outputs;
    for (uint32_t x = 0; x < outputs; x++) {
        axis_span(grid->start_x, grid->span, block.x + x * width, width, &across[x]);
    }
    for (uint32_t y = 0; y < outputs; y++) {
        const CoverSpan *down = rows_of(grid, block.y + y * height, height);
        for (uint32_t x = 0; x < outputs && down->count > 0; x++) {
            add_spans(grid, channel, &across[x], down, samples[y * outputs + x]);
        }
    }
}

void cover_grid_add_color(CoverGrid *grid, CoverRect rect, const int32_t *values, uint32_t channels)
{
    CoverSpan x;
    const CoverSpan *y = rows_of(grid, rect.y, rect.height);
    if (y->count == 0) {
        return;
    }
    axis_span(grid->start_x, grid->span, rect.x, rect.width, &x);
    for (uint32_t channel = 0; channel < channels; channel++) {
        add_spans(grid, channel, &x, y, values[channel]);
    }
}

void cover_grid_row_start(CoverGrid *grid)
{
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        for (uint32_t cell = 0; cell < CELLS; cell++) {
            grid->row[channel][cell] = 0;
        }
    }
}

static void row_add(CoverGrid *grid, uint32_t cell, uint32_t weight, const int32_t *values,
                    uint32_t channels)
{
    for (uint32_t channel = 0; channel < channels; channel++) {
        grid->row[channel][cell] += values[channel] * (int32_t)weight;
    }
}

static void row_add_span(CoverGrid *grid, const CoverSpan *span, const int32_t *values,
                         uint32_t channels)
{
    for (uint32_t cell = 0; cell < span->count; cell++) {
        row_add(grid, span->first + cell, span->weights[cell], values, channels);
    }
}

void cover_grid_row_pixel(CoverGrid *grid, uint32_t x, const int32_t *values, uint32_t channels)
{
    uint32_t span = grid->span;
    if (span < CELLS) {
        CoverSpan cells;
        axis_span(grid->start_x, span, x, 1, &cells);
        row_add_span(grid, &cells, values, channels);
        return;
    }
    uint32_t low = (x - grid->start_x) * CELLS;
    uint32_t cell = low / span;
    uint32_t boundary = (cell + 1) * span;
    uint32_t high = low + CELLS;
    if (high <= boundary) {
        row_add(grid, cell, CELLS * COVER_GRID_WEIGHT_ONE / span, values, channels);
        return;
    }
    row_add(grid, cell, (boundary - low) * COVER_GRID_WEIGHT_ONE / span, values, channels);
    row_add(grid, cell + 1, (high - boundary) * COVER_GRID_WEIGHT_ONE / span, values, channels);
}

void cover_grid_row_finish(CoverGrid *grid, uint32_t y, uint32_t channels)
{
    CoverSpan rows;
    axis_span(grid->start_y, grid->span, y, 1, &rows);
    for (uint32_t row = 0; row < rows.count; row++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            int32_t *sums = grid->sums[channel] + (rows.first + row) * CELLS;
            const int32_t *values = grid->row[channel];
            for (uint32_t cell = 0; cell < CELLS; cell++) {
                int32_t product = values[cell] * (int32_t)rows.weights[row];
                sums[cell] = (int32_t)((uint32_t)sums[cell] + (uint32_t)product);
            }
        }
    }
}

static void axis_totals(uint32_t start, uint32_t span, uint32_t sample, uint32_t *totals)
{
    CoverSpan cells;
    for (uint32_t cell = 0; cell < CELLS; cell++) {
        totals[cell] = 0;
    }
    for (uint32_t index = start / sample; index * sample < start + span; index++) {
        axis_span(start, span, index * sample, sample, &cells);
        for (uint32_t cell = 0; cell < cells.count; cell++) {
            totals[cells.first + cell] += cells.weights[cell];
        }
    }
}

static int32_t mean_q8(int64_t sum, uint64_t weight)
{
    if (weight == 0) {
        return 0;
    }
    int64_t scaled = sum * Q8_ONE;
    int64_t half = (int64_t)(weight / 2);
    return (int32_t)(scaled >= 0 ? (scaled + half) / (int64_t)weight
                                 : -((-scaled + half) / (int64_t)weight));
}

static uint8_t to_byte(int32_t q8)
{
    int32_t value = (q8 + Q8_ONE / 2) / Q8_ONE;
    if (q8 < 0) {
        return 0;
    }
    return (uint8_t)(value > BYTE_MAX ? BYTE_MAX : value);
}

static int32_t scaled(int32_t value, int32_t coefficient)
{
    int64_t product = (int64_t)value * coefficient;
    return (int32_t)(product / (1 << COEFFICIENT_SHIFT));
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)((red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3));
}

static int32_t ink_level(int32_t q8)
{
    return q8 < 0 ? 0 : q8 > INK_FULL_Q8 ? INK_FULL_Q8 : q8;
}

static void apply_black(CoverColor color, int32_t *rgb, int32_t black)
{
    int32_t keep = ink_level(color == COVER_COLOR_CMYK ? INK_FULL_Q8 - black : black);
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        int32_t light =
            color == COVER_COLOR_CMYK_INVERTED ? rgb[channel] : INK_FULL_Q8 - rgb[channel];
        rgb[channel] = (int32_t)((int64_t)ink_level(light) * keep / INK_FULL_Q8);
    }
}

static void ycbcr_to_rgb(const int32_t *q8, int32_t *rgb)
{
    int32_t cb = q8[1] - CHROMA_CENTER_Q8;
    int32_t cr = q8[2] - CHROMA_CENTER_Q8;
    rgb[0] = q8[0] + scaled(cr, CR_TO_R);
    rgb[1] = q8[0] - scaled(cb, CB_TO_G) - scaled(cr, CR_TO_G);
    rgb[2] = q8[0] + scaled(cb, CB_TO_B);
}

static uint16_t pixel(CoverColor color, const int32_t *q8)
{
    if (color == COVER_COLOR_GRAY) {
        uint8_t gray = to_byte(q8[0]);
        return rgb565(gray, gray, gray);
    }
    int32_t rgb[COVER_GRID_CHANNELS] = {q8[0], q8[1], q8[2]};
    if (color == COVER_COLOR_YCBCR || color == COVER_COLOR_YCCK) {
        ycbcr_to_rgb(q8, rgb);
    }
    if (color >= COVER_COLOR_CMYK) {
        apply_black(color, rgb, q8[COVER_GRID_BLACK]);
    }
    return rgb565(to_byte(rgb[0]), to_byte(rgb[1]), to_byte(rgb[2]));
}

static uint32_t used_planes(CoverColor color)
{
    return color == COVER_COLOR_GRAY ? 1 : color >= COVER_COLOR_CMYK ? 4 : COVER_GRID_CHANNELS;
}

static uint32_t totals_of(uint32_t plane)
{
    return plane == COVER_GRID_BLACK ? 0 : plane;
}

static const int32_t *plane_of(const CoverGrid *grid, uint32_t plane)
{
    return plane == COVER_GRID_BLACK ? grid->black : grid->sums[plane];
}

static uint32_t oriented(const CoverGrid *grid, uint32_t x, uint32_t y, uint32_t size)
{
    uint32_t mode = grid->orientation < ORIENTATION_COUNT ? ORIENTATIONS[grid->orientation] : 0;
    uint32_t across = (mode & SWAP) != 0 ? y : x;
    uint32_t down = (mode & SWAP) != 0 ? x : y;
    across = (mode & FLIP_X) != 0 ? size - 1 - across : across;
    down = (mode & FLIP_Y) != 0 ? size - 1 - down : down;
    return down * size + across;
}

static void render_full(const CoverGrid *grid, CoverColor color, uint16_t *full)
{
    int32_t q8[COVER_GRID_BLACK + 1] = {0, 0, 0, 0};
    for (uint32_t cell = 0; cell < COVER_GRID_CELLS; cell++) {
        uint32_t source = oriented(grid, cell % CELLS, cell / CELLS, CELLS);
        for (uint32_t plane = 0; plane < used_planes(color); plane++) {
            uint32_t totals = totals_of(plane);
            uint64_t weight = (uint64_t)grid->totals_x[totals][source % CELLS] *
                              grid->totals_y[totals][source / CELLS];
            q8[plane] = mean_q8(plane_of(grid, plane)[source], weight);
        }
        full[cell] = pixel(color, q8);
    }
}

static int64_t half_sum(const int32_t *sums, uint32_t x, uint32_t y)
{
    uint32_t cell = 2 * y * CELLS + 2 * x;
    return (int64_t)sums[cell] + sums[cell + 1] + sums[cell + CELLS] + sums[cell + CELLS + 1];
}

static uint64_t half_weight(const CoverGrid *grid, uint32_t channel, uint32_t x, uint32_t y)
{
    const uint32_t *across = grid->totals_x[channel];
    const uint32_t *down = grid->totals_y[channel];
    return (uint64_t)(across[2 * x] + across[2 * x + 1]) * (down[2 * y] + down[2 * y + 1]);
}

static void render_half(const CoverGrid *grid, CoverColor color, uint16_t *half)
{
    int32_t q8[COVER_GRID_BLACK + 1] = {0, 0, 0, 0};
    for (uint32_t cell = 0; cell < COVER_GRID_HALF_CELLS; cell++) {
        uint32_t source = oriented(grid, cell % HALF, cell / HALF, HALF);
        uint32_t x = source % HALF;
        uint32_t y = source / HALF;
        for (uint32_t plane = 0; plane < used_planes(color); plane++) {
            q8[plane] = mean_q8(half_sum(plane_of(grid, plane), x, y),
                                half_weight(grid, totals_of(plane), x, y));
        }
        half[cell] = pixel(color, q8);
    }
}

void cover_grid_render(CoverGrid *grid, CoverColor color, uint16_t *full, uint16_t *half)
{
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        axis_totals(grid->start_x, grid->span, grid->sample_width[channel],
                    grid->totals_x[channel]);
        axis_totals(grid->start_y, grid->span, grid->sample_height[channel],
                    grid->totals_y[channel]);
    }
    render_full(grid, color, full);
    render_half(grid, color, half);
}
