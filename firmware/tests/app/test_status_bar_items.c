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

static const uint16_t PORTRAIT_ORDER[] = {
    STATUS_BAR_CLOCK,  STATUS_BAR_PLAYBACK, STATUS_BAR_BLUETOOTH, STATUS_BAR_SHUFFLE,
    STATUS_BAR_VOLUME, STATUS_BAR_POSITION, STATUS_BAR_REPEAT,    STATUS_BAR_PERCENT};

static const uint16_t LANDSCAPE_ORDER[] = {STATUS_BAR_VOLUME, STATUS_BAR_POSITION, STATUS_BAR_CLOCK,
                                           STATUS_BAR_REPEAT, STATUS_BAR_PERCENT};

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
                bool portrait = theme.metrics.status_height > 0;
                int band = portrait ? theme.metrics.status_height : theme.metrics.header_height;
                for (int width = 160; width <= theme.metrics.screen_width; width++) {
                    Theme narrow = theme;
                    narrow.metrics.screen_width = width;
                    StatusBarState state = portrait ? everything() : all_items();
                    check_separate(&narrow, &state, 0, band);
                    check_greedy(&narrow, state, portrait ? PORTRAIT_ORDER : LANDSCAPE_ORDER,
                                 portrait ? 8 : 5);
                }
            }
        }
    }
}

static void portrait_keeps_every_icon_on_the_top_line_at_every_text_size(void)
{
    const char *clocks[] = {"09:41", "12:59 PM"};
    const char *volumes[] = {"47", "60"};
    const char *positions[] = {"3/12", "999/999"};
    for (int compact = 0; compact < 2; compact++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = theme_for(ORIENTATION_PORTRAIT, size, compact != 0);
            for (int variant = 0; variant < 8; variant++) {
                StatusBarState state = everything();
                state.clock = clocks[variant & 1];
                state.volume = volumes[(variant >> 1) & 1];
                state.position = positions[variant >> 2];
                state.percent = (variant & 1) != 0 ? "100%" : "5%";
                state.title_width = 400;
                check_separate(&theme, &state, 0, theme.metrics.status_height);
                uint16_t chosen = status_bar_items(&theme, &state);
                CHECK((chosen & STATUS_BAR_LOCK) != 0);
                CHECK((chosen & STATUS_BAR_CLOCK) != 0);
                state.battery_known = false;
                CHECK((status_bar_items(&theme, &state) & STATUS_BAR_LOCK) != 0);
            }
        }
    }
}

static void portrait_drops_the_same_items_at_every_text_size(void)
{
    uint16_t lead = STATUS_BAR_LOCK | STATUS_BAR_CLOCK | STATUS_BAR_PLAYBACK | STATUS_BAR_BLUETOOTH;
    uint16_t expected[2] = {lead | STATUS_BAR_SHUFFLE, lead};
    uint16_t marked[2] = {STATUS_BAR_LOGO | lead, STATUS_BAR_LOGO | STATUS_BAR_LOCK |
                                                      STATUS_BAR_CLOCK | STATUS_BAR_PLAYBACK};
    for (int compact = 0; compact < 2; compact++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = theme_for(ORIENTATION_PORTRAIT, size, compact != 0);
            StatusBarState state = everything();
            CHECK_EQUAL(expected[compact], status_bar_items(&theme, &state));
            state.logo = true;
            CHECK_EQUAL(marked[compact], status_bar_items(&theme, &state));
        }
    }
}

static void portrait_home_keeps_the_logo_first_and_drops_in_priority_order(void)
{
    for (int compact = 0; compact < 2; compact++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = theme_for(ORIENTATION_PORTRAIT, size, compact != 0);
            for (int width = 160; width <= theme.metrics.screen_width; width++) {
                Theme narrow = theme;
                narrow.metrics.screen_width = width;
                StatusBarState state = everything();
                state.logo = true;
                check_separate(&narrow, &state, 0, narrow.metrics.status_height);
                check_greedy(&narrow, state, PORTRAIT_ORDER, 8);
                StatusBarLayout layout;
                status_bar_layout(&narrow, &state, &layout);
                CHECK((layout.items & (STATUS_BAR_LOGO | STATUS_BAR_LOCK)) ==
                      (STATUS_BAR_LOGO | STATUS_BAR_LOCK));
                Rect logo = status_bar_item_area(&narrow, &state, STATUS_BAR_LOGO);
                CHECK_EQUAL(narrow.metrics.padding, logo.x);
                CHECK_EQUAL(narrow.metrics.status_icon_size, logo.width);
                for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
                    if ((layout.items & (1u << slot)) != 0 && (1u << slot) != STATUS_BAR_LOGO) {
                        CHECK(layout.areas[slot].x > logo.x + logo.width);
                    }
                }
            }
        }
    }
}

static void the_logo_shows_only_on_a_portrait_screen_that_asks_for_it(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = theme_for(orientation, size, false);
            StatusBarState plain = everything();
            plain.title_width = 120;
            StatusBarState marked = plain;
            marked.logo = true;
            StatusBarLayout without;
            StatusBarLayout with;
            status_bar_layout(&theme, &plain, &without);
            status_bar_layout(&theme, &marked, &with);
            CHECK((without.items & STATUS_BAR_LOGO) == 0);
            if (orientation == ORIENTATION_LANDSCAPE) {
                CHECK_EQUAL(without.items, with.items);
                for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
                    if ((with.items & (1u << slot)) != 0) {
                        CHECK_EQUAL(without.areas[slot].x, with.areas[slot].x);
                    }
                }
            } else {
                CHECK((with.items & STATUS_BAR_LOGO) != 0);
            }
        }
    }
}

static void portrait_puts_the_position_left_of_the_clock_and_landscape_protects_the_title(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = BOARD_THEME;
        default_theme_set_text_size(&theme, (TextSize)size);
        StatusBarState state = all_items();
        default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
        state.playback = STATUS_PLAYBACK_NONE;
        state.volume = NULL;
        state.title_width = 300;
        uint16_t portrait = status_bar_items(&theme, &state);
        CHECK((portrait & STATUS_BAR_POSITION) != 0);
        Rect area = status_bar_item_area(&theme, &state, STATUS_BAR_POSITION);
        CHECK(area.y + area.height <= theme.metrics.status_height);
        CHECK(area.x + area.width < status_bar_item_area(&theme, &state, STATUS_BAR_CLOCK).x);
        state = all_items();
        default_theme_set_orientation(&theme, ORIENTATION_LANDSCAPE);
        state.title_width = 300;
        uint16_t shown = status_bar_items(&theme, &state);
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

static void crowded_status_icons_leave_long_titles_untouched(void)
{
    static Color before[HEADER_CAPTURE_PIXELS];
    const char *title = "A long album or settings title that needs the whole header";
    for (int compact = 0; compact < 2; compact++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = BOARD_THEME;
                default_theme_set_text_size(&theme, (TextSize)size);
                default_theme_set_orientation(&theme, (Orientation)orientation);
                default_theme_set_status_bar(&theme, compact != 0);
                StatusBarState state = all_items();
                state.locked = true;
                state.bluetooth = STATUS_BLUETOOTH_CONNECTED;
                state.shuffle = STATUS_SHUFFLE_ALBUMS;
                state.title_width = text_width(theme.fonts.header, title);
                Canvas canvas = canvas_make(header_pixels, (Rect){0, 0, theme.metrics.screen_width,
                                                                  theme.metrics.screen_height});
                canvas_fill_rect(&canvas, canvas.bounds, theme.colors.background);
                header_draw(&canvas, &theme, title);
                memcpy(before, header_pixels, sizeof before);
                status_bar_draw(&canvas, &theme, state);
                Rect area = header_title_area(&theme, theme.fonts.header, title);
                bool unchanged = true;
                for (int y = area.y; y < area.y + area.height; y++) {
                    for (int x = area.x; x < area.x + area.width; x++) {
                        int pixel = y * theme.metrics.screen_width + x;
                        unchanged = unchanged && before[pixel] == header_pixels[pixel];
                    }
                }
                CHECK(unchanged);
                screen_snapshot_write(&canvas, "long-title", compact * 6 + orientation * 3 + size);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(crowded_status_icons_leave_long_titles_untouched),
        TEST_CASE(compact_reclaims_space_and_keeps_the_combined_battery),
        TEST_CASE(every_orientation_and_text_size_respects_fitting_priority),
        TEST_CASE(portrait_keeps_every_icon_on_the_top_line_at_every_text_size),
        TEST_CASE(portrait_drops_the_same_items_at_every_text_size),
        TEST_CASE(portrait_home_keeps_the_logo_first_and_drops_in_priority_order),
        TEST_CASE(the_logo_shows_only_on_a_portrait_screen_that_asks_for_it),
        TEST_CASE(portrait_puts_the_position_left_of_the_clock_and_landscape_protects_the_title),
        TEST_CASE(a_portrait_title_starts_at_the_header_inset_and_a_landscape_one_is_centred),
        TEST_CASE(queue_formatting_accepts_a_future_side_prefix_and_an_empty_queue),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
