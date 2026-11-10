#include <stdint.h>
#include <stdio.h>

#include "ui/icons.h"

enum {
    COLUMNS = 10,
    ROWS = (ICON_COUNT - 2) / COLUMNS + 1,
    MARGIN = 8,
    TENTHS = 10,
    SECTIONS = 5,
    LARGEST_TENTHS = 30,
    CELL = ICON_LARGE_GRID * LARGEST_TENTHS / TENTHS,
    PITCH = CELL + MARGIN,
    SHEET_WIDTH = MARGIN + COLUMNS * PITCH,
    SHEET_CAPACITY = MARGIN + SECTIONS * ROWS * PITCH,
    ROW_BYTES = 1 + 3 * SHEET_WIDTH,
    BLOCK_HEADER = 5,
    ADLER_MODULUS = 65521
};

static const uint8_t SECTION_TENTHS[SECTIONS] = {8, 10, 13, 20, LARGEST_TENTHS};
static const Color BACKGROUND = COLOR_RGB(16, 17, 20);
static const Color PANEL = COLOR_RGB(30, 32, 38);
static const IconInk INK = {COLOR_RGB(242, 242, 245), COLOR_RGB(82, 145, 240)};

static Color pixels[SHEET_WIDTH * SHEET_CAPACITY];

typedef struct Png {
    FILE *file;
    int height;
    uint32_t crc;
    uint32_t sum;
    uint32_t sum_of_sums;
} Png;

static void put(Png *png, const uint8_t *bytes, size_t count)
{
    fwrite(bytes, 1, count, png->file);
    for (size_t index = 0; index < count; index++) {
        png->crc ^= bytes[index];
        for (int bit = 0; bit < 8; bit++) {
            png->crc = (png->crc >> 1) ^ ((png->crc & 1u) != 0 ? 0xEDB88320u : 0u);
        }
    }
}

static void put_number(Png *png, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                        (uint8_t)value};
    put(png, bytes, sizeof bytes);
}

static void begin_chunk(Png *png, const char *type, uint32_t length)
{
    put_number(png, length);
    png->crc = 0xFFFFFFFFu;
    put(png, (const uint8_t *)type, 4);
}

static void end_chunk(Png *png)
{
    put_number(png, ~png->crc);
}

static void put_header(Png *png)
{
    static const uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    static const uint8_t format[] = {8, 2, 0, 0, 0};
    put(png, signature, sizeof signature);
    begin_chunk(png, "IHDR", 2 * 4 + sizeof format);
    put_number(png, SHEET_WIDTH);
    put_number(png, (uint32_t)png->height);
    put(png, format, sizeof format);
    end_chunk(png);
}

static void put_row(Png *png, int y)
{
    uint8_t row[ROW_BYTES] = {0};
    for (int x = 0; x < SHEET_WIDTH; x++) {
        Color color = pixels[y * SHEET_WIDTH + x];
        row[1 + 3 * x] = (uint8_t)((color >> 11) * 255 / 31);
        row[2 + 3 * x] = (uint8_t)((color >> 5 & 63) * 255 / 63);
        row[3 + 3 * x] = (uint8_t)((color & 31) * 255 / 31);
    }
    uint8_t block[BLOCK_HEADER] = {(uint8_t)(y == png->height - 1 ? 1 : 0), (uint8_t)ROW_BYTES,
                                   (uint8_t)(ROW_BYTES >> 8), (uint8_t)~ROW_BYTES,
                                   (uint8_t)(~ROW_BYTES >> 8)};
    put(png, block, sizeof block);
    put(png, row, sizeof row);
    for (int index = 0; index < ROW_BYTES; index++) {
        png->sum = (png->sum + row[index]) % ADLER_MODULUS;
        png->sum_of_sums = (png->sum_of_sums + png->sum) % ADLER_MODULUS;
    }
}

static void put_pixels(Png *png)
{
    static const uint8_t stream[] = {0x78, 0x01};
    uint32_t rows = (uint32_t)png->height;
    begin_chunk(png, "IDAT", sizeof stream + rows * (BLOCK_HEADER + ROW_BYTES) + 4);
    put(png, stream, sizeof stream);
    for (int y = 0; y < png->height; y++) {
        put_row(png, y);
    }
    put_number(png, png->sum_of_sums << 16 | png->sum);
    end_chunk(png);
    begin_chunk(png, "IEND", 0);
    end_chunk(png);
}

static int draw_section(Canvas *canvas, int top, int tenths)
{
    int pitch = ICON_LARGE_GRID * tenths / TENTHS + MARGIN;
    for (int icon = ICON_NONE + 1; icon < ICON_COUNT; icon++) {
        int height = icon_rows((IconId)icon) * tenths / TENTHS;
        Rect area = {MARGIN + (icon - 1) % COLUMNS * PITCH, top + (icon - 1) / COLUMNS * pitch,
                     icon_width((IconId)icon, height), height};
        canvas_fill_rect(canvas, area, PANEL);
        icon_draw(canvas, (IconId)icon, area, INK);
    }
    return top + ROWS * pitch;
}

int main(int count, char **arguments)
{
    Png png = {.file = count == 2 ? fopen(arguments[1], "wb") : NULL, .sum = 1};
    if (png.file == NULL) {
        fputs("usage: icon_sheet FILE.png\n", stderr);
        return 1;
    }
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, SHEET_WIDTH, SHEET_CAPACITY});
    canvas_clear(&canvas, BACKGROUND);
    png.height = MARGIN;
    for (int section = 0; section < SECTIONS; section++) {
        png.height = draw_section(&canvas, png.height, SECTION_TENTHS[section]);
    }
    put_header(&png);
    put_pixels(&png);
    return fclose(png.file) == 0 ? 0 : 1;
}
