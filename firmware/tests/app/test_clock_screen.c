#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "screen_snapshot.h"
#include "screens/calendar_screen.h"
#include "screens/clock_screen.h"
#include "text_spy.h"
#include "wall_time.h"

enum {
    WEDNESDAY_0941_UTC = 1794390060u,
    WEDNESDAY_2330_UTC = 1794439800u
};

static Theme theme;
static Settings settings;
static ClockScreen clock;
static CalendarScreen calendar;
static Color pixels[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];

static void start(uint32_t utc, int16_t offset)
{
    fake_hal_reset();
    fake_hal_set_wall(utc, offset);
    theme = BOARD_THEME;
    calendar_screen_init(&calendar, (CalendarScreenConfig){.theme = &theme});
    clock_screen_init(
        &clock,
        (ClockScreenConfig){.theme = &theme, .settings = &settings, .calendar = &calendar.screen});
}

static Canvas draw(Screen *screen)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    text_spy_reset();
    screen->draw(screen->context, &canvas);
    return canvas;
}

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        const TextSpyEntry *entry = text_spy_entry(index);
        if (strcmp(entry->shown, text) == 0 || strcmp(entry->rest, text) == 0) {
            return true;
        }
    }
    return false;
}

static ScreenAction press(Screen *screen, Key key)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                              100);
}

static void the_clock_shows_the_weekday_and_the_date(void)
{
    start(WEDNESDAY_0941_UTC, 0);
    draw(&clock.screen);
    CHECK(saw(i18n_text(STR_WEDNESDAY)));
    CHECK(saw("November 11, 2026"));
    CHECK(saw(i18n_text(STR_CLOCK_HINT)));
    CHECK(!saw(i18n_text(STR_TIME_UNKNOWN)));
}

static void the_offset_decides_the_local_date(void)
{
    start(WEDNESDAY_2330_UTC, 60);
    draw(&clock.screen);
    CHECK(saw(i18n_text(STR_THURSDAY)));
    CHECK(saw("November 12, 2026"));
    start(WEDNESDAY_0941_UTC, -600);
    draw(&clock.screen);
    CHECK(saw(i18n_text(STR_TUESDAY)));
}

static void without_a_time_both_screens_say_how_to_set_it(void)
{
    start(0, 0);
    draw(&clock.screen);
    CHECK(saw(i18n_text(STR_TIME_UNKNOWN)));
    CHECK(!saw(i18n_text(STR_WEDNESDAY)));
    calendar.screen.enter(&calendar, 0);
    draw(&calendar.screen);
    CHECK(saw(i18n_text(STR_TIME_UNKNOWN)));
    CHECK(saw(i18n_text(STR_CALENDAR)));
    CHECK(!saw(i18n_text(STR_CALENDAR_HINT)));
}

static void play_opens_the_calendar_and_back_leaves_the_clock(void)
{
    start(WEDNESDAY_0941_UTC, 0);
    ScreenAction open = press(&clock.screen, KEY_PLAY);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, open.type);
    CHECK(open.target == &calendar.screen);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(&clock.screen, KEY_BACK).type);
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(&clock.screen, KEY_VOLUME_UP).type);
}

static void the_calendar_opens_on_this_month_and_volume_moves_by_months(void)
{
    start(WEDNESDAY_0941_UTC, 0);
    calendar.screen.enter(&calendar, 0);
    draw(&calendar.screen);
    CHECK(saw("November 2026"));
    press(&calendar.screen, KEY_VOLUME_UP);
    press(&calendar.screen, KEY_VOLUME_UP);
    CHECK_EQUAL(2027, calendar.shown.year);
    CHECK_EQUAL(1, calendar.shown.month);
    for (int step = 0; step < 4; step++) {
        CHECK_EQUAL(SCREEN_ACTION_NONE, press(&calendar.screen, KEY_VOLUME_DOWN).type);
    }
    draw(&calendar.screen);
    CHECK(saw("September 2026"));
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(&calendar.screen, KEY_PLAY).type);
    CHECK_EQUAL(11, calendar.shown.month);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(&calendar.screen, KEY_BACK).type);
}

static int painted(Rect cell, Color color)
{
    int count = 0;
    Canvas canvas = draw(&calendar.screen);
    for (int y = cell.y; y < cell.y + cell.height; y++) {
        for (int x = cell.x; x < cell.x + cell.width; x++) {
            count += canvas_pixel_at(&canvas, x, y) == color ? 1 : 0;
        }
    }
    return count;
}

static void only_today_is_highlighted_and_only_in_its_month(void)
{
    start(WEDNESDAY_0941_UTC, 0);
    calendar.screen.enter(&calendar, 0);
    CalendarGrid grid = calendar_grid(calendar.shown);
    CalendarLayout layout = calendar_screen_layout(&theme, grid.weeks);
    Rect today = calendar_screen_cell(&layout, grid.first_column + 10);
    Rect other = calendar_screen_cell(&layout, grid.first_column + 11);
    Color mark = theme.colors.selection_background;
    CHECK(painted(today, mark) > today.width * today.height / 4);
    CHECK_EQUAL(0, painted(other, mark));
    press(&calendar.screen, KEY_VOLUME_UP);
    CHECK_EQUAL(0, painted(today, mark));
}

static void inside(Rect area, const Theme *shown)
{
    CHECK(area.x >= 0 && area.y >= 0 && area.width > 0 && area.height > 0);
    CHECK(area.x + area.width <= shown->metrics.screen_width);
    CHECK(area.y + area.height <= shown->metrics.screen_height);
}

static void check_clock_layout(const char *digits, const char *meridiem)
{
    ClockLayout layout = clock_screen_layout(&theme, digits, meridiem);
    Rect content = theme_content_area(&theme);
    inside(layout.weekday, &theme);
    inside(layout.time, &theme);
    inside(layout.date, &theme);
    inside(layout.hint, &theme);
    CHECK(layout.weekday.y >= content.y);
    CHECK(layout.weekday.y + layout.weekday.height <= layout.time.y);
    CHECK(layout.time.y + layout.time.height <= layout.date.y);
    CHECK(layout.date.y + layout.date.height <= layout.hint.y);
    int width = text_width(theme.fonts.title, digits) * layout.scale +
                (meridiem != NULL ? text_width(theme.fonts.small, meridiem) : 0);
    CHECK(width <= layout.time.width);
    CHECK(layout.scale >= 1 && layout.scale <= CLOCK_SCREEN_MAX_SCALE);
    CHECK(layout.time.height > font_line_height(theme.fonts.body));
    const char *longest = "September 30, 2026";
    const Font *font = clock_screen_date_font(&theme, longest, layout.date.width);
    CHECK(text_width(font, longest) <= layout.date.width);
    CHECK(font_line_height(font) <= layout.date.height);
    CHECK(text_width(theme.fonts.small, i18n_text(STR_WEDNESDAY)) <= layout.weekday.width);
}

static void check_calendar_layout(void)
{
    CalendarLayout layout = calendar_screen_layout(&theme, CALENDAR_MAX_WEEKS);
    inside(layout.names, &theme);
    inside(layout.grid, &theme);
    inside(layout.hint, &theme);
    CHECK(layout.names.y >= theme_content_area(&theme).y);
    CHECK(layout.names.y + layout.names.height <= layout.grid.y);
    CHECK(layout.grid.y + layout.grid.height <= layout.hint.y);
    CHECK(layout.cell_height >= font_line_height(layout.font));
    CHECK(layout.cell_width >= text_width(layout.font, "28"));
    CHECK(layout.cell_width >= text_width(theme.fonts.small, i18n_text(STR_WEDNESDAY_SHORT)));
}

static void both_screens_fit_every_size_and_orientation(void)
{
    start(WEDNESDAY_0941_UTC, 0);
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            check_clock_layout("23:59", NULL);
            check_clock_layout("12:59", i18n_text(STR_CLOCK_PM));
            check_calendar_layout();
        }
    }
}

static void capture(const char *name, Screen *screen, int variant)
{
    Canvas canvas = draw(screen);
    screen_snapshot_write(&canvas, name, variant);
}

static void every_size_and_orientation_is_captured(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            int variant = orientation * TEXT_SIZE_COUNT + size;
            start(WEDNESDAY_0941_UTC, 0);
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            calendar.screen.enter(&calendar, 0);
            capture("clock", &clock.screen, variant);
            capture("calendar", &calendar.screen, variant);
            fake_hal_set_wall(0, 0);
            capture("clock-unknown", &clock.screen, variant);
            capture("calendar-unknown", &calendar.screen, variant);
        }
    }
}

static void the_clock_reads_twenty_four_hours_until_settings_offer_a_choice(void)
{
    CHECK(wall_time_24h(&settings));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_clock_shows_the_weekday_and_the_date),
        TEST_CASE(the_offset_decides_the_local_date),
        TEST_CASE(without_a_time_both_screens_say_how_to_set_it),
        TEST_CASE(play_opens_the_calendar_and_back_leaves_the_clock),
        TEST_CASE(the_calendar_opens_on_this_month_and_volume_moves_by_months),
        TEST_CASE(only_today_is_highlighted_and_only_in_its_month),
        TEST_CASE(both_screens_fit_every_size_and_orientation),
        TEST_CASE(every_size_and_orientation_is_captured),
        TEST_CASE(the_clock_reads_twenty_four_hours_until_settings_offer_a_choice),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
