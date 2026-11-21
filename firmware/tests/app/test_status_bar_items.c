#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "screen_snapshot.h"
#include "ui/brand_mark.h"
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

static const uint16_t PORTRAIT_ORDER[] = {
    STATUS_BAR_CLOCK,   STATUS_BAR_VOLUME,   STATUS_BAR_POSITION,  STATUS_BAR_REPEAT,
    STATUS_BAR_PERCENT, STATUS_BAR_PLAYBACK, STATUS_BAR_BLUETOOTH, STATUS_BAR_SHUFFLE};

static const uint16_t LANDSCAPE_ORDER[] = {
    STATUS_BAR_VOLUME,  STATUS_BAR_POSITION, STATUS_BAR_CLOCK,     STATUS_BAR_REPEAT,
    STATUS_BAR_PERCENT, STATUS_BAR_PLAYBACK, STATUS_BAR_BLUETOOTH, STATUS_BAR_SHUFFLE};

static const uint16_t *order_for(const Theme *theme)
{
    return theme_is_portrait(theme) ? PORTRAIT_ORDER : LANDSCAPE_ORDER;
}

static void publish(Theme *theme, const StatusBarState *state)
{
    Rect area = status_bar_title_area(theme, state);
    theme->header_room = (HeaderRoom){(int16_t)area.x, (int16_t)(area.x + area.width), 0};
}

static StatusBarState everything(void)
{
    StatusBarState state = all_items();
    state.locked = true;
    state.bluetooth = STATUS_BLUETOOTH_CONNECTED;
    state.shuffle = STATUS_SHUFFLE_ALBUMS;
    return state;
}

static StatusBarState without(StatusBarState state, uint16_t item)
{
    state.clock = item == STATUS_BAR_CLOCK ? NULL : state.clock;
    state.playback = item == STATUS_BAR_PLAYBACK ? STATUS_PLAYBACK_NONE : state.playback;
    state.bluetooth = item == STATUS_BAR_BLUETOOTH ? STATUS_BLUETOOTH_OFF : state.bluetooth;
    state.shuffle = item == STATUS_BAR_SHUFFLE ? STATUS_SHUFFLE_OFF : state.shuffle;
    state.volume = item == STATUS_BAR_VOLUME ? NULL : state.volume;
    state.position = item == STATUS_BAR_POSITION ? NULL : state.position;
    state.repeat = item == STATUS_BAR_REPEAT ? 0 : state.repeat;
    state.percent = item == STATUS_BAR_PERCENT ? NULL : state.percent;
    return state;
}

static Theme theme_for(int orientation, int size, bool compact)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    default_theme_set_status_bar(&theme, compact);
    return theme;
}

static void check_greedy(const Theme *theme, StatusBarState state, const uint16_t *order, int count)
{
    uint16_t chosen = status_bar_items(theme, &state);
    for (int index = count - 1; index >= 0; index--) {
        state = without(state, order[index]);
        chosen &= (uint16_t)~order[index];
        CHECK_EQUAL(chosen, status_bar_items(theme, &state));
    }
}

static void check_separate(const Theme *theme, const StatusBarState *state, int top, int bottom)
{
    StatusBarLayout layout;
    status_bar_layout(theme, state, &layout);
    Rect battery = status_bar_battery_area(theme);
    for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
        if ((layout.items & (1u << slot)) == 0) {
            continue;
        }
        Rect area = layout.areas[slot];
        CHECK(area.width > 0);
        CHECK(area.x >= theme->metrics.padding);
        CHECK(area.y >= top);
        CHECK(area.y + area.height <= bottom);
        CHECK(rect_is_empty(rect_intersection(area, battery)));
        for (int other = slot + 1; other < STATUS_BAR_SLOTS; other++) {
            if ((layout.items & (1u << other)) != 0) {
                CHECK(rect_is_empty(rect_intersection(area, layout.areas[other])));
            }
        }
    }
}

static void every_orientation_and_text_size_respects_fitting_priority(void)
{
    for (int compact = 0; compact < 2; compact++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(orientation, size, compact != 0);
                for (int width = 160; width <= theme.metrics.screen_width; width++) {
                    Theme narrow = theme;
                    narrow.metrics.screen_width = width;
                    StatusBarState state = everything();
                    state.logo = true;
                    check_separate(&narrow, &state, 0, narrow.metrics.header_height);
                    check_greedy(&narrow, state, order_for(&narrow), 8);
                }
            }
        }
    }
}

static void a_long_title_keeps_only_the_logo_the_lock_and_the_battery(void)
{
    for (int compact = 0; compact < 2; compact++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(orientation, size, compact != 0);
                StatusBarState state = everything();
                state.logo = true;
                state.title_width = 600;
                check_separate(&theme, &state, 0, theme.metrics.header_height);
                CHECK_EQUAL(STATUS_BAR_LOGO | STATUS_BAR_LOCK, status_bar_items(&theme, &state));
                Rect title = status_bar_title_area(&theme, &state);
                Rect lock = status_bar_item_area(&theme, &state, STATUS_BAR_LOCK);
                CHECK(title.x + title.width <= lock.x);
                CHECK(lock.x + lock.width <= status_bar_battery_body(&theme).x);
                state.battery_known = false;
                CHECK((status_bar_items(&theme, &state) & STATUS_BAR_LOCK) != 0);
            }
        }
    }
}

static void the_title_outranks_every_optional_item(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = theme_for(orientation, size, false);
            StatusBarState state = everything();
            state.logo = true;
            for (int width = 0; width <= theme.metrics.screen_width; width += 3) {
                state.title_width = width;
                uint16_t items = status_bar_items(&theme, &state);
                Rect title = status_bar_title_area(&theme, &state);
                bool optional = (items & ~(STATUS_BAR_LOGO | STATUS_BAR_LOCK)) != 0;
                CHECK(!optional || width <= title.width);
                for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
                    Rect area = status_bar_item_area(&theme, &state, (uint16_t)(1u << slot));
                    if ((items & (1u << slot)) != 0 && (1u << slot) != STATUS_BAR_LOGO) {
                        CHECK(area.x >= title.x + (width < title.width ? width : title.width));
                    }
                }
            }
        }
    }
}

static void the_logo_leads_the_header_in_both_orientations(void)
{
    for (int compact = 0; compact < 2; compact++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(orientation, size, compact != 0);
                StatusBarState state = everything();
                state.logo = true;
                state.title_width = 60;
                StatusBarLayout layout;
                status_bar_layout(&theme, &state, &layout);
                Rect logo = layout.areas[5];
                CHECK((layout.items & STATUS_BAR_LOGO) != 0);
                CHECK_EQUAL(theme.metrics.padding, logo.x);
                CHECK_EQUAL(theme.metrics.status_icon_size, logo.width);
                CHECK(status_bar_title_area(&theme, &state).x >=
                      logo.x + logo.width + theme.metrics.padding);
                for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
                    if ((layout.items & (1u << slot)) != 0 && (1u << slot) != STATUS_BAR_LOGO) {
                        CHECK(layout.areas[slot].x > logo.x + logo.width);
                    }
                }
                state.logo = false;
                CHECK((status_bar_items(&theme, &state) & STATUS_BAR_LOGO) == 0);
            }
        }
    }
}

static bool is_sun(Color color, int size)
{
    for (int row = 0; row < size; row++) {
        if (color == brand_mark_sun_row(row, size)) {
            return true;
        }
    }
    return false;
}

static Color header_pixels[HEADER_CAPTURE_PIXELS];

static void the_logo_is_drawn_once_at_the_left_of_the_header(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int compact = 0; compact < 2; compact++) {
            Theme theme = theme_for(orientation, TEXT_SIZE_MEDIUM, compact != 0);
            StatusBarState state = everything();
            state.logo = true;
            Canvas canvas = canvas_make(header_pixels, (Rect){0, 0, theme.metrics.screen_width,
                                                              theme.metrics.screen_height});
            header_draw(&canvas, &theme, "");
            status_bar_draw(&canvas, &theme, state);
            Rect logo = status_bar_item_area(&theme, &state, STATUS_BAR_LOGO);
            int size = theme.metrics.status_icon_size;
            int inside = 0;
            for (int y = 0; y < theme.metrics.header_height; y++) {
                for (int x = 0; x < theme.metrics.screen_width; x++) {
                    bool sun = is_sun(canvas_pixel_at(&canvas, x, y), size);
                    bool within = x >= logo.x && x < logo.x + logo.width && y >= logo.y &&
                                  y < logo.y + logo.height;
                    CHECK(!sun || within);
                    inside += sun ? 1 : 0;
                }
            }
            CHECK(inside > size * size / 2);
        }
    }
}

static void the_position_sits_left_of_the_clock_in_both_orientations(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = theme_for(orientation, size, false);
            StatusBarState state = all_items();
            state.playback = STATUS_PLAYBACK_NONE;
            state.volume = NULL;
            state.percent = NULL;
            state.logo = true;
            uint16_t items = status_bar_items(&theme, &state);
            CHECK((items & STATUS_BAR_POSITION) != 0);
            CHECK((items & STATUS_BAR_CLOCK) != 0);
            Rect area = status_bar_item_area(&theme, &state, STATUS_BAR_POSITION);
            CHECK(area.y + area.height <= theme.metrics.header_height);
            CHECK(area.x + area.width < status_bar_item_area(&theme, &state, STATUS_BAR_CLOCK).x);
        }
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

static void a_title_starts_after_the_logo_in_both_orientations(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = BOARD_THEME;
        default_theme_set_orientation(&theme, (Orientation)orientation);
        const Font *font = theme.fonts.header;
        StatusBarState state = all_items();
        state.logo = true;
        state.title_width = text_width(font, "Settings");
        publish(&theme, &state);
        Rect area = header_title_area(&theme, font, "Settings");
        Rect logo = status_bar_item_area(&theme, &state, STATUS_BAR_LOGO);
        CHECK_EQUAL(logo.x + logo.width + theme.metrics.padding, area.x);
        Canvas canvas = canvas_make(
            header_pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
        canvas_fill_rect(&canvas, canvas.bounds, theme.colors.background);
        header_draw(&canvas, &theme, "Settings");
        status_bar_draw(&canvas, &theme, state);
        screen_snapshot_write(&canvas, "header", orientation);
    }
}

static void crowded_status_icons_leave_long_titles_untouched(void)
{
    static Color before[HEADER_CAPTURE_PIXELS];
    const char *title = "A long album or settings title that needs the whole header";
    for (int compact = 0; compact < 2; compact++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(orientation, size, compact != 0);
                StatusBarState state = everything();
                state.logo = true;
                state.title_width = text_width(theme.fonts.header, title);
                publish(&theme, &state);
                Canvas canvas = canvas_make(header_pixels, (Rect){0, 0, theme.metrics.screen_width,
                                                                  theme.metrics.screen_height});
                canvas_fill_rect(&canvas, canvas.bounds, theme.colors.background);
                header_draw(&canvas, &theme, title);
                memcpy(before, header_pixels, sizeof before);
                status_bar_draw(&canvas, &theme, state);
                Rect area = header_title_bounds(&theme);
                bool unchanged = true;
                for (int y = area.y; y < area.y + area.height; y++) {
                    for (int x = area.x; x < area.x + area.width; x++) {
                        int pixel = y * theme.metrics.screen_width + x;
                        unchanged = unchanged && before[pixel] == header_pixels[pixel];
                    }
                }
                CHECK(unchanged);
                screen_snapshot_write(&canvas, "long-title", compact * 9 + orientation * 3 + size);
            }
        }
    }
}

static Color strip_pixels[HEADER_CAPTURE_PIXELS];

static void draw_header_in(Canvas *canvas, const Theme *theme, const StatusBarState *state,
                           const char *title)
{
    canvas_fill_rect(canvas, canvas->clip, theme->colors.background);
    header_draw(canvas, theme, title);
    status_bar_draw(canvas, theme, *state);
}

static void the_header_draws_the_same_whole_or_in_strips(void)
{
    const char *title = "A long album or settings title that needs the whole header";
    static const int STRIPS[] = {23, 49, 7};
    for (int compact = 0; compact < 2; compact++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            Theme theme = theme_for(orientation, TEXT_SIZE_MEDIUM, compact != 0);
            StatusBarState state = everything();
            state.logo = true;
            state.title_width = text_width(theme.fonts.header, title);
            publish(&theme, &state);
            theme.header_room.shift = 17;
            Rect screen = {0, 0, theme.metrics.screen_width, theme.metrics.screen_height};
            Canvas whole = canvas_make(header_pixels, screen);
            draw_header_in(&whole, &theme, &state, title);
            for (int kind = 0; kind < 3; kind++) {
                Canvas strips = canvas_make(strip_pixels, screen);
                for (int y = 0; y < theme.metrics.header_height; y += STRIPS[kind]) {
                    Canvas strip =
                        canvas_clipped(&strips, (Rect){0, y, screen.width, STRIPS[kind]});
                    draw_header_in(&strip, &theme, &state, title);
                }
                int rows = theme.metrics.header_height * screen.width;
                CHECK(memcmp(header_pixels, strip_pixels, (size_t)rows * sizeof(Color)) == 0);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(crowded_status_icons_leave_long_titles_untouched),
        TEST_CASE(the_header_draws_the_same_whole_or_in_strips),
        TEST_CASE(compact_reclaims_space_and_keeps_the_combined_battery),
        TEST_CASE(every_orientation_and_text_size_respects_fitting_priority),
        TEST_CASE(a_long_title_keeps_only_the_logo_the_lock_and_the_battery),
        TEST_CASE(the_title_outranks_every_optional_item),
        TEST_CASE(the_logo_leads_the_header_in_both_orientations),
        TEST_CASE(the_logo_is_drawn_once_at_the_left_of_the_header),
        TEST_CASE(the_position_sits_left_of_the_clock_in_both_orientations),
        TEST_CASE(a_title_starts_after_the_logo_in_both_orientations),
        TEST_CASE(queue_formatting_accepts_a_future_side_prefix_and_an_empty_queue),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
