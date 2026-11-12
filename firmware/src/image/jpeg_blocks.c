#include "jpeg_internal.h"

enum {
    COSINE_ONE = 4096,
    COSINE_STEPS = 32,
    KERNEL_SHIFT = 12,
    OUTPUT_SHIFT = 26,
    BLOCK_SIDE = 8,
    LEVEL_SHIFT = 128,
    SAMPLE_MAX = 255,
    DELTA_LIMIT = 4095,
    COEFFICIENT_LIMIT = 32767
};

static const int32_t COSINES[9] = {4096, 4017, 3784, 3406, 2896, 2276, 1567, 799, 0};

static int32_t cosine(uint32_t step)
{
    step %= COSINE_STEPS;
    if (step <= 8) {
        return COSINES[step];
    }
    if (step <= 16) {
        return -COSINES[16 - step];
    }
    if (step <= 24) {
        return -COSINES[step - 16];
    }
    return COSINES[32 - step];
}

static int32_t averaged_cosine(uint32_t position, uint32_t frequency, uint32_t shift)
{
    uint32_t width = 1u << shift;
    int32_t sum = 0;
    for (uint32_t pixel = position * width; pixel < (position + 1) * width; pixel++) {
        sum += cosine((2 * pixel + 1) * frequency);
    }
    int32_t half = (int32_t)width / 2;
    return sum >= 0 ? (sum + half) / (int32_t)width : -((-sum + half) / (int32_t)width);
}

void jpeg_prepare_kernel(JpegDecoder *decoder)
{
    for (uint32_t position = 0; position < decoder->outputs; position++) {
        for (uint32_t frequency = 0; frequency < BLOCK_SIDE; frequency++) {
            int32_t value = averaged_cosine(position, frequency, decoder->shift);
            if (frequency == 0) {
                value = (value * COSINES[4] + COSINE_ONE / 2) / COSINE_ONE;
            }
            decoder->kernel[position][frequency] = value;
        }
    }
}

bool jpeg_coefficient_used(const JpegDecoder *decoder, uint32_t natural)
{
    bool across = false;
    bool down = false;
    for (uint32_t position = 0; position < decoder->outputs; position++) {
        across = across || decoder->kernel[position][natural % BLOCK_SIDE] != 0;
        down = down || decoder->kernel[position][natural / BLOCK_SIDE] != 0;
    }
    return across && down;
}

static int32_t clamp(int64_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : (int32_t)value;
}

static int32_t dequantized(const JpegDecoder *decoder, uint32_t component, uint32_t index)
{
    const int32_t *quant = decoder->quant[decoder->components[component].quant];
    int64_t value = (int64_t)decoder->coefficients[index] * quant[index];
    return clamp(value, -COEFFICIENT_LIMIT, COEFFICIENT_LIMIT);
}

static CoverRect block_rect(const JpegDecoder *decoder, uint32_t component, uint32_t block_x,
                            uint32_t block_y)
{
    const JpegComponent *info = &decoder->components[component];
    uint32_t width = BLOCK_SIDE * (decoder->h_max / info->h);
    uint32_t height = BLOCK_SIDE * (decoder->v_max / info->v);
    return (CoverRect){block_x * width, block_y * height, width, height};
}

bool jpeg_block_needed(const JpegDecoder *decoder, uint32_t component, uint32_t block_x,
                       uint32_t block_y)
{
    return cover_grid_touches(decoder->grid, block_rect(decoder, component, block_x, block_y));
}

bool jpeg_block_row_past_crop(const JpegDecoder *decoder, uint32_t component, uint32_t block_y)
{
    CoverRect rect = block_rect(decoder, component, 0, block_y);
    return rect.y >= decoder->grid->start_y + decoder->grid->span;
}

static bool dequantize_rows(const JpegDecoder *decoder, uint32_t component, int32_t *values,
                            bool *rows_used)
{
    bool any = false;
    for (uint32_t v = 0; v < BLOCK_SIDE; v++) {
        rows_used[v] = false;
        for (uint32_t u = 0; u < BLOCK_SIDE; u++) {
            uint32_t index = v * BLOCK_SIDE + u;
            values[index] =
                decoder->coefficients[index] == 0 ? 0 : dequantized(decoder, component, index);
            rows_used[v] = rows_used[v] || values[index] != 0;
        }
        any = any || rows_used[v];
    }
    return any;
}

static int32_t row_sum(const int32_t *kernel, const int32_t *row)
{
    int32_t sum = 0;
    for (uint32_t u = 0; u < BLOCK_SIDE; u++) {
        sum += kernel[u] * row[u];
    }
    return sum;
}

static int32_t column_sample(const JpegDecoder *decoder, const bool *rows_used,
                             const int32_t *column, uint32_t stride, uint32_t y)
{
    int64_t sum = 0;
    for (uint32_t v = 0; v < BLOCK_SIDE; v++) {
        sum += rows_used[v] ? (int64_t)decoder->kernel[y][v] * column[v * stride] : 0;
    }
    int64_t rounded = sum + ((int64_t)1 << (OUTPUT_SHIFT - 1));
    return clamp(rounded >> OUTPUT_SHIFT, -DELTA_LIMIT, DELTA_LIMIT);
}

static void inverse_two(const JpegDecoder *decoder, const int32_t *values, const bool *rows_used,
                        int32_t *samples)
{
    int32_t rows[BLOCK_SIDE * 2];
    for (uint32_t v = 0; v < BLOCK_SIDE; v++) {
        const int32_t *row = values + v * BLOCK_SIDE;
        rows[v * 2] = rows_used[v] ? row_sum(decoder->kernel[0], row) : 0;
        rows[v * 2 + 1] = rows_used[v] ? row_sum(decoder->kernel[1], row) : 0;
    }
    for (uint32_t y = 0; y < 2; y++) {
        samples[y * 2] = column_sample(decoder, rows_used, rows, 2, y);
        samples[y * 2 + 1] = column_sample(decoder, rows_used, rows + 1, 2, y);
    }
}

static void inverse_four(const JpegDecoder *decoder, const int32_t *values, const bool *rows_used,
                         int32_t *samples)
{
    int32_t rows[BLOCK_SIDE * 4];
    for (uint32_t v = 0; v < BLOCK_SIDE; v++) {
        for (uint32_t x = 0; x < 4; x++) {
            rows[v * 4 + x] =
                rows_used[v] ? row_sum(decoder->kernel[x], values + v * BLOCK_SIDE) : 0;
        }
    }
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            samples[y * 4 + x] = column_sample(decoder, rows_used, rows + x, 4, y);
        }
    }
}

void jpeg_inverse_general(const JpegDecoder *decoder, const int32_t *values, const bool *rows_used,
                          int32_t *samples)
{
    uint32_t outputs = decoder->outputs;
    int32_t rows[BLOCK_SIDE * BLOCK_SIDE];
    for (uint32_t v = 0; v < BLOCK_SIDE; v++) {
        for (uint32_t x = 0; x < outputs; x++) {
            rows[v * outputs + x] =
                rows_used[v] ? row_sum(decoder->kernel[x], values + v * BLOCK_SIDE) : 0;
        }
    }
    for (uint32_t y = 0; y < outputs; y++) {
        for (uint32_t x = 0; x < outputs; x++) {
            samples[y * outputs + x] = column_sample(decoder, rows_used, rows + x, outputs, y);
        }
    }
}

void jpeg_inverse_block(const JpegDecoder *decoder, const int32_t *values, const bool *rows_used,
                        int32_t *samples)
{
    if (decoder->outputs == 2) {
        inverse_two(decoder, values, rows_used, samples);
    } else if (decoder->outputs == 4) {
        inverse_four(decoder, values, rows_used, samples);
    } else {
        jpeg_inverse_general(decoder, values, rows_used, samples);
    }
}

static void inverse(const JpegDecoder *decoder, uint32_t component, int32_t *samples)
{
    int32_t values[JPEG_BLOCK];
    bool rows_used[BLOCK_SIDE];
    dequantize_rows(decoder, component, values, rows_used);
    jpeg_inverse_block(decoder, values, rows_used, samples);
}

static bool any_used_coefficient(const JpegDecoder *decoder)
{
    for (uint32_t natural = 0; natural < JPEG_BLOCK; natural++) {
        if (decoder->coefficients[natural] != 0 && jpeg_coefficient_used(decoder, natural)) {
            return true;
        }
    }
    return false;
}

static void add_samples(JpegDecoder *decoder, uint32_t component, CoverRect block, int32_t *samples,
                        bool whole)
{
    uint32_t count = decoder->outputs * decoder->outputs;
    for (uint32_t index = 0; whole && index < count; index++) {
        samples[index] = clamp((int64_t)samples[index] + LEVEL_SHIFT, 0, SAMPLE_MAX);
    }
    cover_grid_add_block(decoder->grid, component, block, samples, decoder->outputs);
}

static int32_t dc_level(const JpegDecoder *decoder, uint32_t component)
{
    int64_t level = dequantized(decoder, component, 0);
    return (int32_t)(level >= 0 ? (level + 4) / 8 : -((-level + 4) / 8));
}

void jpeg_output_block(JpegDecoder *decoder, uint32_t component, uint32_t block_x, uint32_t block_y,
                       JpegBlockKind kind)
{
    CoverRect block = block_rect(decoder, component, block_x, block_y);
    if (!cover_grid_touches(decoder->grid, block)) {
        return;
    }
    if (kind == JPEG_BLOCK_DC_FIRST || kind == JPEG_BLOCK_DC_DELTA ||
        (kind == JPEG_BLOCK_WHOLE && decoder->outputs == 1)) {
        int32_t level = dc_level(decoder, component);
        level += kind == JPEG_BLOCK_DC_DELTA ? 0 : LEVEL_SHIFT;
        level = kind == JPEG_BLOCK_WHOLE ? clamp(level, 0, SAMPLE_MAX) : level;
        cover_grid_add(decoder->grid, component, block, clamp(level, -DELTA_LIMIT, DELTA_LIMIT));
        return;
    }
    if (kind == JPEG_BLOCK_AC_DELTA && !any_used_coefficient(decoder)) {
        return;
    }
    int32_t samples[JPEG_BLOCK];
    inverse(decoder, component, samples);
    add_samples(decoder, component, block, samples, kind == JPEG_BLOCK_WHOLE);
}
