#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "gfx/text.h"
#include "ui/status_bar.h"

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

static void every_orientation_and_text_size_respects_fitting_priority(void)
{
    for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
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
                    CHECK(area.x >= narrow.metrics.padding);
                    CHECK(area.x + area.width <= status_bar_battery_body(&narrow).x);
                    CHECK(area.y >= 0);
                    int band = narrow.metrics.status_height > 0 ? narrow.metrics.status_height
                                                                : narrow.metrics.header_height;
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

static void portrait_centres_the_position_and_landscape_protects_the_title(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = BOARD_THEME;
        default_theme_set_text_size(&theme, (TextSize)size);
        StatusBarState state = all_items();
        default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
        CHECK((status_bar_items(&theme, &state) & STATUS_BAR_POSITION) != 0);
        Rect area = status_bar_item_area(&theme, &state, STATUS_BAR_POSITION);
        CHECK_EQUAL((theme.metrics.screen_width - area.width) / 2, area.x);
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

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_orientation_and_text_size_respects_fitting_priority),
        TEST_CASE(portrait_centres_the_position_and_landscape_protects_the_title),
        TEST_CASE(queue_formatting_accepts_a_future_side_prefix_and_an_empty_queue),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
