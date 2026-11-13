#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "default_theme.h"
#include "gfx/text.h"
#include "ui/label.h"
#include "ui/message_screen.h"

static void capture_progress(const char *directory)
{
    static Color pixels[480 * 480];
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            Rect area = {0, 0, theme.metrics.screen_width, theme.metrics.screen_height};
            Canvas canvas = canvas_make(pixels, area);
            for (int phase = 0; phase < 3; phase++) {
                char text[160];
                StringBuilder builder = string_builder_make(text, sizeof text);
                if (phase == 2) {
                    string_builder_append(&builder, i18n_text(STR_CHECK_CHANGED));
                } else if (phase == 0) {
                    string_builder_append(&builder, i18n_text(STR_CHECKING_MUSIC));
                } else {
                    i18n_format_counts(&builder, STR_CHECK_COUNTS, phase ? 120 : 0,
                                       phase ? 1240 : 0);
                    if (phase) {
                        string_builder_append_char(&builder, '\n');
                        i18n_format_counts(&builder, STR_CHECK_ELAPSED, 12, 0);
                    }
                }
                WireMotion motion = {0};
                MessageScreen message = {0};
                wire_motion_run(&motion, &message, WIRE_SHAPE_DISC, 0);
                wire_motion_tick(&motion, phase ? 12000 : 0);
                Screen *screen =
                    message_screen_init(&message, (MessageConfig){.title = i18n_text(STR_MUSIC),
                                                                  .message = text,
                                                                  .theme = &theme,
                                                                  .motion = &motion});
                CHECK(label_wrap_count(theme.fonts.body, text,
                                       area.width - 2 * theme.metrics.padding) *
                          font_line_height(theme.fonts.body) <
                      theme_content_area(&theme).height);
                canvas_clear(&canvas, theme.colors.background);
                screen->draw(screen->context, &canvas);
                if (directory == NULL) {
                    continue;
                }
                char path[1024];
                snprintf(path, sizeof path, "%s/check-%d-%d-%d.ppm", directory, size, orientation,
                         phase);
                FILE *file = fopen(path, "wb");
                CHECK(file != NULL);
                if (file == NULL) {
                    return;
                }
                fprintf(file, "P6\n%d %d\n255\n", area.width, area.height);
                for (int index = 0; index < area.width * area.height; index++) {
                    uint16_t pixel = pixels[index];
                    uint8_t rgb[] = {(uint8_t)((pixel >> 11) * 255 / 31),
                                     (uint8_t)(((pixel >> 5) & 63) * 255 / 63),
                                     (uint8_t)((pixel & 31) * 255 / 31)};
                    fwrite(rgb, 1, sizeof rgb, file);
                }
                fclose(file);
            }
        }
    }
}

static void progress_fits_all_sizes_and_orientations(void)
{
    capture_progress(NULL);
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {TEST_CASE(progress_fits_all_sizes_and_orientations)};
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    if (argc == 2) {
        capture_progress(argv[1]);
    }
    return result;
}
