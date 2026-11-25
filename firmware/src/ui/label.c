#include "ui/label.h"

#include "core/utf8.h"

enum {
    ELLIPSIS_CODEPOINT = 0x2026,
    HYPHEN = '-',
    FADE_STEPS = 15,
    CJK_MARKS = 0x30,
    FULL_WIDTH_FORMS = 0xFF,
    BRACKETS_FIRST = 0x3008,
    BRACKETS_LAST = 0x301B,
    MARKS_NO_START = 28
};

static const char ELLIPSIS_TEXT[] = "\xE2\x80\xA6";

static const uint8_t NO_START[] = {0x01, 0x02, 0x05, 0x41, 0x43, 0x45, 0x47, 0x49, 0x63, 0x83,
                                   0x85, 0x87, 0x8E, 0x9D, 0x9E, 0xA1, 0xA3, 0xA5, 0xA7, 0xA9,
                                   0xC3, 0xE3, 0xE5, 0xE7, 0xEE, 0xFB, 0xFC, 0xFD, 0x01, 0x09,
                                   0x0C, 0x0E, 0x1A, 0x1B, 0x1F, 0x3D, 0x5D};

static int advance_of(const Font *font, uint32_t codepoint)
{
    const Glyph *glyph = font_find_glyph(font, codepoint);
    return glyph != NULL ? glyph->advance : 0;
}

static bool is_wide(uint32_t codepoint)
{
    return (codepoint >= 0x2E80 && codepoint <= 0xD7AF) ||
           (codepoint >= 0xF900 && codepoint <= 0xFFEF);
}

static bool is_bracket(uint32_t codepoint, uint32_t closing)
{
    return codepoint >= BRACKETS_FIRST && codepoint <= BRACKETS_LAST && (codepoint & 1u) == closing;
}

static bool starts_no_line(uint32_t codepoint)
{
    uint32_t page = codepoint >> 8;
    int first = page == CJK_MARKS ? 0 : MARKS_NO_START;
    int end = page == FULL_WIDTH_FORMS ? (int)sizeof NO_START : MARKS_NO_START;
    for (int index = first; index < end; index++) {
        if (NO_START[index] == (codepoint & 0xFFu)) {
            return true;
        }
    }
    return is_bracket(codepoint, 1);
}

static bool label_breaks_between(uint32_t previous, uint32_t next)
{
    if (previous == HYPHEN) {
        return next >= 'A';
    }
    return (is_wide(previous) || is_wide(next)) && !starts_no_line(next) &&
           !is_bracket(previous, 0) && previous != 0xFF08;
}

static LabelFit fit_until(const Font *font, const char *text, const char *end, int max_width)
{
    int ellipsis_width = advance_of(font, ELLIPSIS_CODEPOINT);
    int used = 0;
    const char *cursor = text;
    const char *cut = text;
    const char *word = NULL;
    uint32_t previous = 0;
    while (cursor != end) {
        const char *before = cursor;
        uint32_t codepoint = utf8_next(&cursor);
        if (codepoint == 0) {
            break;
        }
        if (before != text && used + ellipsis_width <= max_width &&
            (previous == ' ' || label_breaks_between(previous, codepoint))) {
            word = before;
        }
        used += advance_of(font, codepoint);
        if (used + ellipsis_width <= max_width) {
            cut = cursor;
        }
        if (used > max_width) {
            cut = word != NULL ? word : cut;
            return (LabelFit){.byte_count = (size_t)(cut - text), .ellipsis = true};
        }
        previous = codepoint;
    }
    return (LabelFit){.byte_count = (size_t)(cursor - text), .ellipsis = false};
}

LabelFit label_fit(const Font *font, const char *text, int max_width)
{
    return fit_until(font, text, NULL, max_width);
}

int label_baseline(const Font *font, Rect area)
{
    int top = area.y + (area.height - font_line_height(font)) / 2;
    return top + font->ascent;
}

Rect label_centered_area(const Font *font, Rect bounds, const char *text)
{
    int width = text_width(font, text);
    if (width > bounds.width) {
        width = bounds.width;
    }
    return (Rect){bounds.x + (bounds.width - width) / 2, bounds.y, width, bounds.height};
}

void label_draw_serviced(Canvas *canvas, TextStyle style, Label label, void (*service)(void))
{
    Canvas clipped = canvas_clipped(canvas, label.area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    LabelFit fit = label_fit(style.font, label.text, label.area.width);
    if (service != NULL) {
        service();
    }
    Point pen = {label.area.x, label_baseline(style.font, label.area)};
    pen.x = text_draw_span(&clipped, style, pen, (TextSpan){label.text, fit.byte_count});
    if (fit.ellipsis) {
        if (service != NULL) {
            service();
        }
        text_draw(&clipped, style, pen, ELLIPSIS_TEXT);
    }
}

void label_draw(Canvas *canvas, TextStyle style, Label label)
{
    label_draw_serviced(canvas, style, label, NULL);
}

static bool prefers_break_after(uint32_t codepoint)
{
    return codepoint == 0x3001 || codepoint == 0x3002 || codepoint == 0xFF0C;
}

size_t label_wrap_line(const Font *font, const char *text, int max_width)
{
    if (text == NULL) {
        return 0;
    }
    int used = 0;
    const char *cursor = text;
    const char *fitting = text;
    bool has_fit = false;
    const char *preferred = NULL;
    uint32_t previous = 0;
    for (;;) {
        const char *before = cursor;
        uint32_t codepoint = utf8_next(&cursor);
        bool ends = codepoint == ' ' || codepoint == '\n' || codepoint == 0;
        if (ends || (before != text && label_breaks_between(previous, codepoint))) {
            if (used <= max_width || !has_fit) {
                fitting = before;
                has_fit = true;
                preferred =
                    prefers_break_after(previous) && used * 2 >= max_width ? before : preferred;
            }
            if ((ends && codepoint != ' ') || used > max_width) {
                break;
            }
        }
        used += advance_of(font, codepoint);
        previous = codepoint;
    }
    const char *chosen = preferred != NULL && used > max_width ? preferred : fitting;
    return chosen != NULL ? (size_t)(chosen - text) : 0;
}

static const char *after_wrap(const char *cursor)
{
    while (*cursor == ' ') {
        cursor++;
    }
    return *cursor == '\n' ? cursor + 1 : cursor;
}

int label_wrap_count(const Font *font, const char *text, int max_width)
{
    int count = 0;
    const char *cursor = text;
    do {
        cursor = after_wrap(cursor + label_wrap_line(font, cursor, max_width));
        count++;
    } while (*cursor != '\0');
    return count;
}

static int width_until(const Font *font, const char *text, const char *end)
{
    int width = 0;
    while (text < end) {
        uint32_t codepoint = utf8_next(&text);
        if (codepoint == 0) {
            break;
        }
        width += advance_of(font, codepoint);
    }
    return width;
}

typedef struct WrappedLine {
    Rect row;
    const char *text;
    const char *end;
    bool centered;
    void (*service)(void);
} WrappedLine;

typedef struct WrappedLabel {
    Label label;
    bool centered;
    void (*service)(void);
} WrappedLabel;

static void serve(void (*service)(void))
{
    if (service != NULL) {
        service();
    }
}

static void draw_line(Canvas *canvas, TextStyle style, WrappedLine line)
{
    LabelFit fit = fit_until(style.font, line.text, line.end, line.row.width);
    serve(line.service);
    int indent = 0;
    if (line.centered) {
        int width = width_until(style.font, line.text, line.text + fit.byte_count) +
                    (fit.ellipsis ? advance_of(style.font, ELLIPSIS_CODEPOINT) : 0);
        serve(line.service);
        indent = (line.row.width - width) / 2;
    }
    Point pen = {line.row.x + indent, line.row.y + style.font->ascent};
    pen.x = text_draw_span(canvas, style, pen, (TextSpan){line.text, fit.byte_count});
    serve(line.service);
    if (fit.ellipsis) {
        text_draw(canvas, style, pen, ELLIPSIS_TEXT);
    }
}

static void draw_wrapped(Canvas *canvas, TextStyle style, const WrappedLabel *wrapped)
{
    Label label = wrapped->label;
    bool centered = wrapped->centered;
    void (*service)(void) = wrapped->service;
    Canvas clipped = canvas_clipped(canvas, label.area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    int line_height = font_line_height(style.font);
    int room = label.area.height / line_height;
    int lines = label_wrap_count(style.font, label.text, label.area.width);
    serve(service);
    lines = lines > room ? room : lines;
    lines = lines < 1 ? 1 : lines;
    int top = centered ? (label.area.height - lines * line_height) / 2 : 0;
    Rect row = {label.area.x, label.area.y + top, label.area.width, line_height};
    const char *cursor = label.text;
    for (int line = 1; line <= lines; line++, row.y += line_height) {
        const char *end = NULL;
        if (line < lines) {
            end = cursor + label_wrap_line(style.font, cursor, label.area.width);
            serve(service);
        }
        draw_line(&clipped, style, (WrappedLine){row, cursor, end, centered, service});
        serve(service);
        if (end == NULL) {
            break;
        }
        cursor = after_wrap(end);
    }
}

void label_draw_wrapped(Canvas *canvas, TextStyle style, Label label)
{
    draw_wrapped(canvas, style, &(WrappedLabel){label, true, NULL});
}

void label_draw_wrapped_serviced(Canvas *canvas, TextStyle style, Label label,
                                 void (*service)(void))
{
    draw_wrapped(canvas, style, &(WrappedLabel){label, true, service});
}

void label_draw_paragraph(Canvas *canvas, TextStyle style, Label label)
{
    draw_wrapped(canvas, style, &(WrappedLabel){label, false, NULL});
}

static void fade_edges(Canvas *canvas, Rect area, Marquee marquee)
{
    int right = area.x + area.width - 1;
    for (int step = 0; step < FADE_STEPS && step < area.width / 2; step++) {
        uint8_t alpha = (uint8_t)(FADE_STEPS - step);
        for (int y = area.y; y < area.y + area.height; y++) {
            canvas_blend_pixel(canvas, (Point){right - step, y}, marquee.fade, alpha);
            if (marquee.offset > 0) {
                canvas_blend_pixel(canvas, (Point){area.x + step, y}, marquee.fade, alpha);
            }
        }
    }
}

void label_draw_scrolling(Canvas *canvas, TextStyle style, Label label, Marquee marquee)
{
    Canvas clipped = canvas_clipped(canvas, label.area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    int baseline = label_baseline(style.font, label.area);
    int start = label.area.x - marquee.offset;
    int repeat_start = start + text_width(style.font, label.text) + marquee.gap;
    text_draw(&clipped, style, (Point){start, baseline}, label.text);
    text_draw(&clipped, style, (Point){repeat_start, baseline}, label.text);
    if (marquee.fades) {
        fade_edges(&clipped, label.area, marquee);
    }
}
