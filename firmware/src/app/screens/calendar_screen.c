#include "calendar_screen.h"

#include "clock_screen.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "gfx/text.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"
#include "wall_time.h"

enum {
    TITLE_CAPACITY = 48,
    NUMBER_CAPACITY = 4,
    MONTH_ARGUMENTS = 2
};

_Static_assert(STR_SUNDAY_SHORT - STR_MONDAY_SHORT == CALENDAR_WEEK_DAYS - 1,
               "short weekdays are in order");

static const char WIDEST_DAY[] = "28";

static int fitting_cell(const Theme *theme, Rect room, uint8_t weeks, const Font **font)
{
    int body = font_line_height(theme->fonts.body);
    int small = font_line_height(theme->fonts.small);
    int pad = theme->metrics.padding;
    int height = room.height / weeks;
    bool roomy =
        height >= body && room.width >= text_width(theme->fonts.body, WIDEST_DAY) + pad / 2;
    *font = roomy ? theme->fonts.body : theme->fonts.small;
    int line = font_line_height(*font) + pad;
    height = height < line ? height : line;
    return height > small ? height : small;
}

CalendarLayout calendar_screen_layout(const Theme *theme, uint8_t weeks)
{
    Rect content = theme_content_area(theme);
    int pad = theme->metrics.padding;
    int small = font_line_height(theme->fonts.small);
    int width = content.width - 2 * pad;
    int cell_width = width / CALENDAR_WEEK_DAYS;
    int left = pad + (width - cell_width * CALENDAR_WEEK_DAYS) / 2;
    Rect hint = {pad, content.y + content.height - small - pad / 2, width, small};
    Rect names = {left, content.y + pad / 2, cell_width * CALENDAR_WEEK_DAYS, small};
    int room = hint.y - names.y - names.height - pad / 2;
    const Font *font = theme->fonts.body;
    int cell_height = fitting_cell(theme, (Rect){0, 0, cell_width, room}, weeks, &font);
    Rect grid = {left, names.y + names.height + (room - cell_height * weeks) / 2, names.width,
                 cell_height * weeks};
    return (CalendarLayout){.names = names,
                            .grid = grid,
                            .hint = hint,
                            .cell_width = cell_width,
                            .cell_height = cell_height,
                            .font = font};
}

Rect calendar_screen_cell(const CalendarLayout *layout, int index)
{
    return (Rect){layout->grid.x + index % CALENDAR_WEEK_DAYS * layout->cell_width,
                  layout->grid.y + index / CALENDAR_WEEK_DAYS * layout->cell_height,
                  layout->cell_width, layout->cell_height};
}

static void draw_centered(Canvas *canvas, TextStyle style, Rect area, const char *text)
{
    label_draw(canvas, style,
               (Label){.area = label_centered_area(style.font, area, text), .text = text});
}

static void month_title(CalendarMonth month, char *text, size_t capacity)
{
    char year[NUMBER_CAPACITY + 2];
    StringBuilder year_text = string_builder_make(year, sizeof year);
    string_builder_append_unsigned(&year_text, month.year);
    const char *arguments[MONTH_ARGUMENTS] = {i18n_text(clock_screen_month(month.month)), year};
    StringBuilder builder = string_builder_make(text, capacity);
    i18n_format(&builder, STR_CALENDAR_MONTH, arguments, MONTH_ARGUMENTS);
}

static void draw_names(Canvas *canvas, const Theme *theme, const CalendarLayout *layout)
{
    for (int day = 0; day < CALENDAR_WEEK_DAYS; day++) {
        Rect cell = {layout->names.x + day * layout->cell_width, layout->names.y,
                     layout->cell_width, layout->names.height};
        draw_centered(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary}, cell,
                      i18n_text((StringId)(STR_MONDAY_SHORT + day)));
    }
}

static void draw_day(Canvas *canvas, const Theme *theme, Rect cell, const CalendarLayout *layout,
                     int day, bool today)
{
    char number[NUMBER_CAPACITY];
    StringBuilder builder = string_builder_make(number, sizeof number);
    string_builder_append_unsigned(&builder, (uint32_t)day);
    Color ink = today ? theme->colors.selection_text : theme->colors.text;
    if (today) {
        int side = cell.width < cell.height ? cell.width : cell.height;
        canvas_fill_rect(
            canvas,
            (Rect){cell.x + (cell.width - side) / 2, cell.y + (cell.height - side) / 2, side, side},
            theme->colors.selection_background);
    }
    draw_centered(canvas, (TextStyle){layout->font, ink}, cell, number);
}

static int today_in(CalendarMonth month)
{
    WallNow now = wall_time_now();
    CalendarDate today = calendar_date(now.local_s / CALENDAR_DAY_SECONDS);
    return now.known && today.year == month.year && today.month == month.month ? today.day : 0;
}

static void draw_month(Canvas *canvas, const CalendarScreen *calendar)
{
    const Theme *theme = calendar->config.theme;
    CalendarGrid grid = calendar_grid(calendar->shown);
    CalendarLayout layout = calendar_screen_layout(theme, grid.weeks);
    int today = today_in(calendar->shown);
    draw_names(canvas, theme, &layout);
    for (int day = 1; day <= grid.days; day++) {
        Rect cell = calendar_screen_cell(&layout, grid.first_column + day - 1);
        draw_day(canvas, theme, cell, &layout, day, day == today);
    }
    draw_centered(canvas, (TextStyle){theme->fonts.small, theme->colors.text_tertiary}, layout.hint,
                  i18n_text(STR_CALENDAR_HINT));
}

static void draw(void *context, Canvas *canvas)
{
    CalendarScreen *calendar = context;
    const Theme *theme = calendar->config.theme;
    Rect content = theme_content_area(theme);
    char title[TITLE_CAPACITY];
    canvas_fill_rect(canvas, content, theme->colors.background);
    calendar->screen.header_title_width = 0;
    if (!wall_time_now().known) {
        header_draw(canvas, theme, i18n_text(STR_CALENDAR));
        clock_screen_draw_unknown(canvas, theme, content);
        return;
    }
    month_title(calendar->shown, title, sizeof title);
    calendar->screen.header_title_width = (uint16_t)text_width(theme->fonts.header, title);
    header_draw(canvas, theme, title);
    draw_month(canvas, calendar);
}

static CalendarMonth this_month(void)
{
    CalendarDate today = calendar_date(wall_time_now().local_s / CALENDAR_DAY_SECONDS);
    return (CalendarMonth){today.year, today.month};
}

static void enter(void *context, uint32_t now_ms)
{
    CalendarScreen *calendar = context;
    (void)now_ms;
    calendar->shown = this_month();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    CalendarScreen *calendar = context;
    (void)now_ms;
    switch (navigation_from_key(event).intent) {
    case NAVIGATION_PREVIOUS:
        calendar->shown = calendar_step(calendar->shown, -1);
        return screen_action_none();
    case NAVIGATION_NEXT:
        calendar->shown = calendar_step(calendar->shown, 1);
        return screen_action_none();
    case NAVIGATION_SELECT:
        calendar->shown = this_month();
        return screen_action_none();
    default:
        return navigation_exit_action(event);
    }
}

Screen *calendar_screen_init(CalendarScreen *calendar, CalendarScreenConfig config)
{
    *calendar = (CalendarScreen){.config = config, .shown = this_month()};
    calendar->screen = (Screen){.title = i18n_text(STR_CALENDAR),
                                .context = calendar,
                                .enter = enter,
                                .handle_key = handle_key,
                                .draw = draw};
    return &calendar->screen;
}
