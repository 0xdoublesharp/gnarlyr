#include "clock_screen.h"

#include "core/calendar.h"
#include "core/clock_face.h"
#include "core/i18n.h"
#include "core/utf8.h"
#include "gfx/text.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"
#include "wall_time.h"

enum {
    NUMBER_CAPACITY = 12,
    DIGITS_CAPACITY = 8,
    DATE_ARGUMENTS = 3
};

_Static_assert(STR_SUNDAY - STR_MONDAY == CALENDAR_WEEK_DAYS - 1, "weekdays are in order");
_Static_assert(STR_DECEMBER - STR_JANUARY == CALENDAR_MONTHS - 1, "months are in order");

StringId clock_screen_weekday(uint8_t weekday)
{
    return (StringId)(STR_MONDAY + weekday % CALENDAR_WEEK_DAYS);
}

StringId clock_screen_month(uint8_t month)
{
    return (StringId)(STR_JANUARY + (month - 1) % CALENDAR_MONTHS);
}

void clock_screen_date_text(uint32_t local_s, char *text, int capacity)
{
    CalendarDate date = calendar_date(local_s / CALENDAR_DAY_SECONDS);
    char day[NUMBER_CAPACITY];
    char year[NUMBER_CAPACITY];
    StringBuilder day_text = string_builder_make(day, sizeof day);
    StringBuilder year_text = string_builder_make(year, sizeof year);
    string_builder_append_unsigned(&day_text, date.day);
    string_builder_append_unsigned(&year_text, date.year);
    const char *arguments[DATE_ARGUMENTS] = {day, i18n_text(clock_screen_month(date.month)), year};
    StringBuilder builder = string_builder_make(text, (size_t)capacity);
    i18n_format(&builder, STR_CLOCK_DATE, arguments, DATE_ARGUMENTS);
}

static int digits_width(const Theme *theme, const char *digits, const char *meridiem, int scale)
{
    int width = text_width(theme->fonts.title, digits) * scale;
    return meridiem != NULL
               ? width + theme->metrics.padding / 2 + text_width(theme->fonts.small, meridiem)
               : width;
}

static int fitting_scale(const Theme *theme, const char *digits, const char *meridiem,
                         int room_height)
{
    int width = theme->metrics.screen_width - 2 * theme->metrics.padding;
    int line = font_line_height(theme->fonts.title);
    int scale = CLOCK_SCREEN_MAX_SCALE;
    while (scale > 1 &&
           (digits_width(theme, digits, meridiem, scale) > width || scale * line > room_height)) {
        scale--;
    }
    return scale;
}

ClockLayout clock_screen_layout(const Theme *theme, const char *digits, const char *meridiem)
{
    Rect content = theme_content_area(theme);
    int pad = theme->metrics.padding;
    int gap = pad / 2;
    int small = font_line_height(theme->fonts.small);
    int body = font_line_height(theme->fonts.body);
    int width = content.width - 2 * pad;
    Rect hint = {pad, content.y + content.height - small - gap, width, small};
    int room = hint.y - content.y - pad;
    int scale = fitting_scale(theme, digits, meridiem, room - small - body - 2 * gap);
    int time_height = scale * font_line_height(theme->fonts.title);
    int top = content.y + gap + (room - (small + body + time_height + 2 * gap)) / 2;
    return (ClockLayout){.weekday = {pad, top, width, small},
                         .time = {pad, top + small + gap, width, time_height},
                         .date = {pad, top + small + 2 * gap + time_height, width, body},
                         .hint = hint,
                         .scale = scale};
}

const Font *clock_screen_date_font(const Theme *theme, const char *date, int width)
{
    return text_width(theme->fonts.body, date) <= width ? theme->fonts.body : theme->fonts.small;
}

static void draw_centered(Canvas *canvas, TextStyle style, Rect area, const char *text)
{
    label_draw(canvas, style,
               (Label){.area = label_centered_area(style.font, area, text), .text = text});
}

static int draw_scaled(Canvas *canvas, TextStyle style, Point pen, ClockLayout layout,
                       const char *text)
{
    const char *cursor = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(style.font, codepoint);
        if (glyph != NULL) {
            int advance = glyph->advance * layout.scale;
            text_draw_glyph_scaled(canvas, style, (Rect){pen.x, pen.y, advance, layout.time.height},
                                   codepoint, layout.scale);
            pen.x += advance;
        }
    }
    return pen.x;
}

static void draw_time(Canvas *canvas, const Theme *theme, ClockLayout layout, ClockFace face)
{
    char digits[DIGITS_CAPACITY];
    StringBuilder builder = string_builder_make(digits, sizeof digits);
    clock_face_digits(&builder, face);
    const char *mark =
        face.meridiem != CLOCK_TWENTY_FOUR_HOUR ? i18n_text(clock_face_meridiem(face)) : NULL;
    int width = digits_width(theme, digits, mark, layout.scale);
    Point pen = {layout.time.x + (layout.time.width - width) / 2, layout.time.y};
    int end = draw_scaled(canvas, (TextStyle){theme->fonts.title, theme->colors.text}, pen, layout,
                          digits);
    if (mark != NULL) {
        int small = font_line_height(theme->fonts.small);
        Rect area = {end + theme->metrics.padding / 2,
                     layout.time.y + layout.time.height - small - layout.scale, width, small};
        label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                   (Label){.area = area, .text = mark});
    }
}

static void draw_known(Canvas *canvas, const ClockScreen *clock, uint32_t local_s)
{
    const Theme *theme = clock->config.theme;
    ClockFace face = clock_face(local_s, wall_time_24h(clock->config.settings));
    char digits[DIGITS_CAPACITY];
    char date[CLOCK_SCREEN_TEXT];
    StringBuilder builder = string_builder_make(digits, sizeof digits);
    clock_face_digits(&builder, face);
    const char *mark =
        face.meridiem != CLOCK_TWENTY_FOUR_HOUR ? i18n_text(clock_face_meridiem(face)) : NULL;
    ClockLayout layout = clock_screen_layout(theme, digits, mark);
    CalendarDate today = calendar_date(local_s / CALENDAR_DAY_SECONDS);
    clock_screen_date_text(local_s, date, sizeof date);
    draw_centered(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                  layout.weekday, i18n_text(clock_screen_weekday(today.weekday)));
    draw_time(canvas, theme, layout, face);
    const Font *font = clock_screen_date_font(theme, date, layout.date.width);
    draw_centered(canvas, (TextStyle){font, theme->colors.text}, layout.date, date);
}

void clock_screen_draw_unknown(Canvas *canvas, const Theme *theme, Rect content)
{
    int pad = theme->metrics.padding;
    int width = content.width - 2 * pad;
    int title = font_line_height(theme->fonts.title);
    const char *hint = i18n_text(STR_TIME_UNKNOWN_HINT);
    int lines = label_wrap_count(theme->fonts.body, hint, width);
    int hint_height = lines * font_line_height(theme->fonts.body);
    int room = clock_screen_layout(theme, "", NULL).hint.y - content.y;
    int top = content.y + (room - (title + pad / 2 + hint_height)) / 2;
    draw_centered(canvas, (TextStyle){theme->fonts.title, theme->colors.text},
                  (Rect){pad, top, width, title}, i18n_text(STR_TIME_UNKNOWN));
    label_draw_wrapped(
        canvas, (TextStyle){theme->fonts.body, theme->colors.text_secondary},
        (Label){.area = {pad, top + title + pad / 2, width, hint_height}, .text = hint});
}

static void draw(void *context, Canvas *canvas)
{
    const ClockScreen *clock = context;
    const Theme *theme = clock->config.theme;
    Rect content = theme_content_area(theme);
    WallNow now = wall_time_now();
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, i18n_text(STR_CLOCK));
    if (now.known) {
        draw_known(canvas, clock, now.local_s);
    } else {
        clock_screen_draw_unknown(canvas, theme, content);
    }
    ClockLayout layout = clock_screen_layout(theme, "", NULL);
    draw_centered(canvas, (TextStyle){theme->fonts.small, theme->colors.text_tertiary}, layout.hint,
                  i18n_text(STR_CLOCK_HINT));
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    const ClockScreen *clock = context;
    (void)now_ms;
    if (navigation_from_key(event).intent == NAVIGATION_SELECT && clock->config.calendar != NULL) {
        return screen_action_push(clock->config.calendar);
    }
    return navigation_exit_action(event);
}

Screen *clock_screen_init(ClockScreen *clock, ClockScreenConfig config)
{
    *clock = (ClockScreen){.config = config};
    clock->screen = (Screen){
        .title = i18n_text(STR_CLOCK), .context = clock, .handle_key = handle_key, .draw = draw};
    return &clock->screen;
}
