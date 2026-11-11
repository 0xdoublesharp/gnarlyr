#include "set_clock_screen.h"

#include "core/clock_face.h"
#include "core/i18n.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"

enum {
    FIRST_YEAR = 2024,
    LAST_YEAR = 2099,
    DEFAULT_YEAR = 2026,
    OFFSET_STEP = 15
};

static const StringId FIELDS[] = {STR_YEAR,           STR_SET_CLOCK_MONTH,  STR_SET_CLOCK_DAY,
                                  STR_SET_CLOCK_HOUR, STR_SET_CLOCK_MINUTE, STR_SET_CLOCK_ZONE};

static void enter(void *context, uint32_t now_ms)
{
    (void)now_ms;
    SetClockScreen *editor = context;
    HalWallTime wall = hal_clock_wall();
    uint32_t local = clock_local_seconds(wall.utc_s, wall.offset_minutes);
    editor->date = wall.source == HAL_WALL_UNKNOWN ? (CalendarDate){DEFAULT_YEAR, 1, 1, 0}
                                                   : calendar_date(local / CALENDAR_DAY_SECONDS);
    editor->date.year = editor->date.year < FIRST_YEAR  ? FIRST_YEAR
                        : editor->date.year > LAST_YEAR ? LAST_YEAR
                                                        : editor->date.year;
    editor->hour = wall.source == HAL_WALL_UNKNOWN ? 0 : (uint8_t)(local / 3600 % 24);
    editor->minute = wall.source == HAL_WALL_UNKNOWN ? 0 : (uint8_t)(local / 60 % 60);
    editor->offset_minutes = wall.offset_minutes;
    uint8_t days = calendar_month_days((CalendarMonth){editor->date.year, editor->date.month});
    editor->date.day = editor->date.day > days ? days : editor->date.day;
    editor->field = 0;
}

static int wrap(int value, int low, int high)
{
    return value < low ? high : value > high ? low : value;
}

static void change(SetClockScreen *editor, int delta)
{
    CalendarDate *date = &editor->date;
    switch (editor->field) {
    case SET_CLOCK_YEAR:
        date->year = (uint16_t)(date->year + delta);
        date->year = date->year < FIRST_YEAR  ? FIRST_YEAR
                     : date->year > LAST_YEAR ? LAST_YEAR
                                              : date->year;
        break;
    case SET_CLOCK_MONTH:
        date->month = (uint8_t)wrap(date->month + delta, 1, 12);
        break;
    case SET_CLOCK_DAY:
        date->day = (uint8_t)wrap(date->day + delta, 1,
                                  calendar_month_days((CalendarMonth){date->year, date->month}));
        break;
    case SET_CLOCK_HOUR:
        editor->hour = (uint8_t)wrap(editor->hour + delta, 0, 23);
        break;
    case SET_CLOCK_MINUTE:
        editor->minute = (uint8_t)wrap(editor->minute + delta, 0, 59);
        break;
    case SET_CLOCK_OFFSET:
        editor->offset_minutes =
            (int16_t)wrap(editor->offset_minutes + delta * OFFSET_STEP, -720, 840);
        break;
    }
    uint8_t days = calendar_month_days((CalendarMonth){date->year, date->month});
    date->day = date->day > days ? days : date->day;
}

static ScreenAction save(SetClockScreen *editor, uint32_t now_ms)
{
    uint32_t local = calendar_days(editor->date) * CALENDAR_DAY_SECONDS + editor->hour * 3600u +
                     editor->minute * 60u;
    HalWallTime time = {.utc_s = (uint32_t)((int64_t)local - editor->offset_minutes * 60),
                        .offset_minutes = editor->offset_minutes,
                        .source = HAL_WALL_PLAYER};
    bool saved = hal_clock_set_wall(time);
    toast_show(editor->config.toast, i18n_text(saved ? STR_SET_CLOCK_SAVED : STR_SET_CLOCK_FAILED),
               now_ms);
    return saved ? screen_action_pop() : screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    SetClockScreen *editor = context;
    if (event.key == KEY_MENU) {
        if (event.gesture == KEY_GESTURE_PRESS || event.gesture == KEY_GESTURE_LONG_PRESS) {
            editor->field =
                (uint8_t)wrap(editor->field + (event.gesture == KEY_GESTURE_PRESS ? 1 : -1), 0,
                              SET_CLOCK_FIELDS - 1);
        }
        return screen_action_none();
    }
    NavigationIntent intent = navigation_from_key(event).intent;
    if (intent == NAVIGATION_SELECT) {
        return save(editor, now_ms);
    }
    if (intent == NAVIGATION_NEXT || intent == NAVIGATION_PREVIOUS) {
        change(editor, intent == NAVIGATION_NEXT ? 1 : -1);
    }
    return navigation_exit_action(event);
}

static void number(StringBuilder *text, uint32_t value, int width)
{
    if (width == 2 && value < 10) {
        string_builder_append_char(text, '0');
    }
    string_builder_append_unsigned(text, value);
}

static void date_text(StringBuilder *text, CalendarDate date)
{
    number(text, date.year, 4);
    string_builder_append_char(text, '-');
    number(text, date.month, 2);
    string_builder_append_char(text, '-');
    number(text, date.day, 2);
}

const char *set_clock_screen_summary(const Settings *settings)
{
    static char summary[SET_CLOCK_TEXT_CAPACITY];
    HalWallTime wall = hal_clock_wall();
    StringBuilder text = string_builder_make(summary, sizeof summary);
    if (wall.source == HAL_WALL_UNKNOWN) {
        string_builder_append(&text, i18n_text(STR_SET_CLOCK_NOT_SET));
    } else {
        uint32_t local = clock_local_seconds(wall.utc_s, wall.offset_minutes);
        date_text(&text, calendar_date(local / CALENDAR_DAY_SECONDS));
        string_builder_append_char(&text, ' ');
        clock_face_text(&text, clock_face(local, settings->clock_24h));
    }
    return summary;
}

static void value_text(SetClockScreen *editor)
{
    StringBuilder text = string_builder_make(editor->text, sizeof editor->text);
    uint32_t value = editor->field == SET_CLOCK_YEAR    ? editor->date.year
                     : editor->field == SET_CLOCK_MONTH ? editor->date.month
                     : editor->field == SET_CLOCK_DAY   ? editor->date.day
                     : editor->field == SET_CLOCK_HOUR  ? editor->hour
                                                        : editor->minute;
    if (editor->field == SET_CLOCK_OFFSET) {
        int offset = editor->offset_minutes;
        string_builder_append_char(&text, offset < 0 ? '-' : '+');
        offset = offset < 0 ? -offset : offset;
        number(&text, (uint32_t)offset / 60, 2);
        string_builder_append_char(&text, ':');
        number(&text, (uint32_t)offset % 60, 2);
    } else if (editor->field == SET_CLOCK_HOUR) {
        clock_face_text(&text, clock_face(editor->hour * 3600u + editor->minute * 60u,
                                          editor->config.settings->clock_24h));
    } else {
        number(&text, value, editor->field == SET_CLOCK_YEAR ? 4 : 2);
    }
}

static void centered(Canvas *canvas, const Theme *theme, Rect row, const char *text)
{
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.text};
    label_draw(canvas, style, (Label){label_centered_area(style.font, row, text), text});
}

static void draw(void *context, Canvas *canvas)
{
    SetClockScreen *editor = context;
    const Theme *theme = editor->config.theme;
    Rect area = theme_content_area(theme);
    int pad = theme->metrics.padding;
    int line = font_line_height(theme->fonts.small);
    int hint_lines =
        label_wrap_count(theme->fonts.small, i18n_text(STR_SET_CLOCK_HINT), area.width - 2 * pad);
    Rect hint = {pad, area.y + area.height - hint_lines * line - pad, area.width - 2 * pad,
                 hint_lines * line};
    int row_height = (hint.y - area.y) / 3;
    Rect row = {pad, area.y, area.width - 2 * pad, row_height};
    header_draw(canvas, theme, editor->screen.title);
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){row, set_clock_screen_summary(editor->config.settings)});
    row.y += row_height;
    centered(canvas, theme, row, i18n_text(FIELDS[editor->field]));
    row.y += row_height;
    value_text(editor);
    canvas_fill_rect(canvas, row, theme->colors.selection_background);
    centered(canvas, theme, row, editor->text);
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text},
                       (Label){hint, i18n_text(STR_SET_CLOCK_HINT)});
}

Screen *set_clock_screen_init(SetClockScreen *editor, SetClockConfig config)
{
    *editor = (SetClockScreen){.config = config};
    editor->screen = (Screen){.title = i18n_text(STR_SET_CLOCK),
                              .context = editor,
                              .hold_volume_keys = true,
                              .enter = enter,
                              .handle_key = handle_key,
                              .draw = draw};
    return &editor->screen;
}
