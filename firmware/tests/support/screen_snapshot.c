#include "screen_snapshot.h"

#include <stdio.h>
#include <stdlib.h>

#include "check.h"

void screen_snapshot_write(const Canvas *canvas, const char *name, int variant)
{
    const char *directory = getenv("ECHO_TEST_SCREENSHOTS");
    if (directory == NULL) {
        return;
    }
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s-%d.ppm", directory, name, variant);
    CHECK(length > 0 && (size_t)length < sizeof path);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", canvas->bounds.width, canvas->bounds.height);
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            uint32_t color = canvas_pixel_at(canvas, x, y);
            uint8_t rgb[] = {(uint8_t)((color >> 11) * 255 / 31),
                             (uint8_t)((color >> 5 & 63) * 255 / 63),
                             (uint8_t)((color & 31) * 255 / 31)};
            CHECK_EQUAL(sizeof rgb, fwrite(rgb, 1, sizeof rgb, file));
        }
    }
    CHECK_EQUAL(0, fclose(file));
}
