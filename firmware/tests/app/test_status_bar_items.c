#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "screen_snapshot.h"
#include "ui/header.h"
#include "ui/status_bar.h"

enum {
    HEADER_CAPTURE_PIXELS = HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE
};

static StatusBarState all_items(void)
{
    return (StatusBarState){.playback = STATUS_PLAYBACK_PLAYING,
                            .battery_known = true,
                            .battery_percent = 50,
                            .volume = "47",
                            .position = "3/12",
                            .clock = "09:41",
                            .repeat = 1,
                            .percent = "50%"};
}

static void fitting_priority(bool compact)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            default_theme_set_status_bar(&theme, compact);
            for (int width = 80; width <= theme.metrics.screen_width; width++) {
                Theme narrow = theme;
                narrow.metrics.screen_width = width;
                StatusBarState state = all_items();
                StatusBarLayout layout;
                status_bar_layout(&narrow, &state, &layout);
                uint8_t chosen = layout.items;
                for (int item = 0; item < 5; item++) {
                    if ((chosen & (1u << item)) == 0) {
                        continue;
                    }
                    Rect area = layout.areas[item];
                    bool split = narrow.metrics.status_height > 0 &&
                                 narrow.metrics.header_height > narrow.metrics.status_height;
                    int limit = split ? narrow.metrics.screen_width - narrow.metrics.padding
                                      : status_bar_battery_body(&narrow).x;
                    CHECK(area.x >= narrow.metrics.padding);
                    CHECK(area.x + area.width <= limit);
                    CHECK(area.y >= (split ? narrow.metrics.status_height : 0));
                    int band = split || narrow.metrics.status_height == 0
                                   ? narrow.metrics.header_height
                                   : narrow.metrics.status_height;
                    CHECK(area.y + area.height <= band);
                    for (int other = item + 1; other < 5; other++) {
                        if ((chosen & (1u << other)) != 0) {
                            CHECK(rect_is_empty(rect_intersection(area, layout.areas[other])));
                        }
                    }
                }
                state.percent = NULL;
                CHECK_EQUAL(chosen & ~STATUS_BAR_PERCENT, status_bar_items(&narrow, &state));
                state.repeat = 0;
                CHECK_EQUAL(chosen & ~(STATUS_BAR_PERCENT | STATUS_BAR_REPEAT),
                            status_bar_items(&narrow, &state));
                state.clock = NULL;
                CHECK_EQUAL(chosen & (STATUS_BAR_VOLUME | STATUS_BAR_POSITION),
                            status_bar_items(&narrow, &state));
                state.position = NULL;
                CHECK_EQUAL(chosen & STATUS_BAR_VOLUME, status_bar_items(&narrow, &state));
            }
        }
    }
}

static void every_orientation_and_text_size_respects_fitting_priority(void)
{
    fitting_priority(false);
    fitting_priority(true);
}

static void portrait_puts_the_position_on_line_two_and_landscape_protects_the_title(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = BOARD_THEME;
        default_theme_set_text_size(&theme, (TextSize)size);
        StatusBarState state = all_items();
        default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
        uint8_t portrait = status_bar_items(&theme, &state);
        CHECK((portrait & STATUS_BAR_POSITION) != 0);
        Rect area = status_bar_item_area(&theme, &state, STATUS_BAR_POSITION);
        CHECK(area.y >= theme.metrics.status_height);
        if ((portrait & STATUS_BAR_CLOCK) != 0) {
            CHECK(area.x + area.width < status_bar_item_area(&theme, &state, STATUS_BAR_CLOCK).x);
        }
        default_theme_set_orientation(&theme, ORIENTATION_LANDSCAPE);
        state.title_width = 300;
        uint8_t shown = status_bar_items(&theme, &state);
        CHECK((shown & STATUS_BAR_POSITION) == 0);
        CHECK((shown & STATUS_BAR_VOLUME) != 0);
        CHECK((shown & STATUS_BAR_CLOCK) == 0);
        state.title_width = 0;
        CHECK((status_bar_items(&theme, &state) & STATUS_BAR_POSITION) != 0);
    }
}

static void queue_formatting_accepts_a_future_side_prefix_and_an_empty_queue(void)
{
    char text[32];
    StringBuilder builder = string_builder_make(text, sizeof text);
    status_bar_position_text(&builder, (StatusQueuePosition){3, 8, "A"});
    CHECK(strcmp(text, "A 3/8") == 0);
    builder = string_builder_make(text, sizeof text);
    status_bar_position_text(&builder, (StatusQueuePosition){65535, 65535, NULL});
    CHECK(strcmp(text, "65535/65535") == 0);
    builder = string_builder_make(text, sizeof text);
    status_bar_position_text(&builder, (StatusQueuePosition){0, 0, NULL});
    CHECK(strcmp(text, "") == 0);
}

static void compact_reclaims_space_and_keeps_the_combined_battery(void)
{
    static Color pixels[480 * 480];
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_orientation(&theme, (Orientation)orientation);
            default_theme_set_text_size(&theme, (TextSize)size);
            int rows = theme_visible_rows(&theme);
            int height = theme.metrics.header_height;
            default_theme_set_status_bar(&theme, true);
            CHECK(theme.metrics.header_height < height);
            CHECK(theme_visible_rows(&theme) >= rows);
            if (orientation != ORIENTATION_LANDSCAPE && size == TEXT_SIZE_LARGE) {
                CHECK(theme_visible_rows(&theme) > rows);
            }
            CHECK(theme.metrics.row_height >= font_line_height(theme.fonts.body));
            CHECK(theme.metrics.status_icon_size < 14);
            StatusBarState state = all_items();
            state.battery_percent = 100;
            state.charging = true;
            state.usb_power = true;
            state.locked = true;
            state.bluetooth = STATUS_BLUETOOTH_CONNECTED;
            state.shuffle = STATUS_SHUFFLE_ALBUMS;
            Rect battery = status_bar_battery_body(&theme);
            CHECK(text_width(theme.fonts.small, "100%") <= battery.width - 21);
            CHECK(battery.y >= 0);
            CHECK(battery.y + battery.height <= theme.metrics.header_height);
            CHECK((status_bar_items(&theme, &state) & STATUS_BAR_PERCENT) == 0);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            canvas_fill_rect(&canvas, canvas.bounds, theme.colors.background);
            header_draw(&canvas, &theme, "");
            status_bar_draw(&canvas, &theme, state);
            for (int y = battery.y + 2; y < battery.y + battery.height - 4; y++) {
                CHECK_EQUAL(theme.colors.header_background,
                            canvas_pixel_at(&canvas, battery.x + 3, y));
            }
            int terminal = battery.x + battery.width;
            CHECK_EQUAL(theme.colors.header_background,
                        canvas_pixel_at(&canvas, terminal, battery.y + 4));
            CHECK(canvas_pixel_at(&canvas, terminal, battery.y + battery.height / 2) !=
                  theme.colors.header_background);
            CHECK_EQUAL(theme.colors.header_background,
                        canvas_pixel_at(&canvas, terminal, battery.y + battery.height - 5));
            screen_snapshot_write(&canvas, "compact", orientation * 3 + size);
            default_theme_set_status_bar(&theme, false);
            CHECK_EQUAL(height, theme.metrics.header_height);
        }
    }
}

static Color header_pixels[HEADER_CAPTURE_PIXELS];

static void a_portrait_title_starts_at_the_header_inset_and_a_landscape_one_is_centred(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = BOARD_THEME;
        default_theme_set_orientation(&theme, (Orientation)orientation);
        const Font *font = theme.fonts.header;
        Rect bounds = header_title_bounds(&theme);
        Rect area = header_title_area(&theme, font, "Settings");
        int width = text_width(font, "Settings");
        int centred = bounds.x + (bounds.width - width) / 2;
        CHECK_EQUAL(orientation == ORIENTATION_PORTRAIT ? bounds.x : centred, area.x);
        Canvas canvas = canvas_make(
            header_pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
        canvas_fill_rect(&canvas, canvas.bounds, theme.colors.background);
        header_draw(&canvas, &theme, "Settings");
        StatusBarState state = all_items();
        state.title_width = text_width(font, "Settings");
        status_bar_draw(&canvas, &theme, state);
        screen_snapshot_write(&canvas, "header", orientation);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(compact_reclaims_space_and_keeps_the_combined_battery),
        TEST_CASE(every_orientation_and_text_size_respects_fitting_priority),
        TEST_CASE(portrait_puts_the_position_on_line_two_and_landscape_protects_the_title),
        TEST_CASE(a_portrait_title_starts_at_the_header_inset_and_a_landscape_one_is_centred),
        TEST_CASE(queue_formatting_accepts_a_future_side_prefix_and_an_empty_queue),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
