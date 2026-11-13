#include "ui/label.h"

#include "core/utf8.h"

enum {
    ELLIPSIS_CODEPOINT = 0x2026
};

static const char ELLIPSIS_TEXT[] = "\xE2\x80\xA6";

static int advance_of(const Font *font, uint32_t codepoint)
{
    const Glyph *glyph = font_find_glyph(font, codepoint);
    return glyph != NULL ? glyph->advance : 0;
}

static LabelFit fit_until(const Font *font, const char *text, const char *end, int max_width)
{
    int ellipsis_width = advance_of(font, ELLIPSIS_CODEPOINT);
    int used = 0;
    const char *cursor = text;
    const char *cut = text;
    while (cursor != end) {
        uint32_t codepoint = utf8_next(&cursor);
        if (codepoint == 0) {
            break;
        }
        used += advance_of(font, codepoint);
        if (used + ellipsis_width <= max_width) {
            cut = cursor;
        }
        if (used > max_width) {
            return (LabelFit){.byte_count = (size_t)(cut - text), .ellipsis = true};
        }
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

void label_draw(Canvas *canvas, TextStyle style, Label label)
{
    Canvas clipped = canvas_clipped(canvas, label.area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    LabelFit fit = label_fit(style.font, label.text, label.area.width);
    Point pen = {label.area.x, label_baseline(style.font, label.area)};
    pen.x = text_draw_span(&clipped, style, pen, (TextSpan){label.text, fit.byte_count});
    if (fit.ellipsis) {
        text_draw(&clipped, style, pen, ELLIPSIS_TEXT);
    }
}

size_t label_wrap_line(const Font *font, const char *text, int max_width)
{
    int used = 0;
    const char *cursor = text;
    const char *fitting = NULL;
    for (;;) {
        const char *before = cursor;
        uint32_t codepoint = utf8_next(&cursor);
        if (codepoint == ' ' || codepoint == '\n' || codepoint == 0) {
            if (used <= max_width || fitting == NULL) {
                fitting = before;
            }
            if (codepoint != ' ' || used > max_width) {
                break;
            }
        }
        used += advance_of(font, codepoint);
    }
    return (size_t)(fitting - text);
}

static const char *next_line(const Font *font, const char *text, int max_width)
{
    const char *cursor = text + label_wrap_line(font, text, max_width);
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
        cursor = next_line(font, cursor, max_width);
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
    int width = width_until(style.font, line.text, line.text + fit.byte_count) +
                (fit.ellipsis ? advance_of(style.font, ELLIPSIS_CODEPOINT) : 0);
    serve(line.service);
    int indent = line.centered ? (line.row.width - width) / 2 : 0;
    Point pen = {line.row.x + indent, line.row.y + style.font->ascent};
    pen.x = text_draw_span(canvas, style, pen, (TextSpan){line.text, fit.byte_count});
    serve(line.service);
    if (fit.ellipsis) {
        text_draw(canvas, style, pen, ELLIPSIS_TEXT);
    }
}

static void draw_wrapped(Canvas *canvas, TextStyle style, WrappedLabel wrapped)
{
    Label label = wrapped.label;
    bool centered = wrapped.centered;
    Canvas clipped = canvas_clipped(canvas, label.area);
    int line_height = font_line_height(style.font);
    int room = label.area.height / line_height;
    int lines = label_wrap_count(style.font, label.text, label.area.width);
    serve(wrapped.service);
    lines = lines > room ? room : lines;
    lines = lines < 1 ? 1 : lines;
    int top = centered ? (label.area.height - lines * line_height) / 2 : 0;
    Rect row = {label.area.x, label.area.y + top, label.area.width, line_height};
    const char *cursor = label.text;
    for (int line = 1; line <= lines; line++, row.y += line_height) {
        size_t bytes = label_wrap_line(style.font, cursor, label.area.width);
        serve(wrapped.service);
        const char *end = line < lines ? cursor + bytes : NULL;
        draw_line(&clipped, style, (WrappedLine){row, cursor, end, centered, wrapped.service});
        serve(wrapped.service);
        cursor = next_line(style.font, cursor, label.area.width);
        serve(wrapped.service);
    }
}

void label_draw_wrapped(Canvas *canvas, TextStyle style, Label label)
{
    draw_wrapped(canvas, style, (WrappedLabel){label, true, NULL});
}

void label_draw_wrapped_serviced(Canvas *canvas, TextStyle style, Label label,
                                 void (*service)(void))
{
    draw_wrapped(canvas, style, (WrappedLabel){label, true, service});
}

void label_draw_paragraph(Canvas *canvas, TextStyle style, Label label)
{
    draw_wrapped(canvas, style, (WrappedLabel){label, false, NULL});
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
}
