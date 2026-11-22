#include "ui/footer_hints.h"

#include <stdbool.h>

#include "gfx/text.h"
#include "ui/label.h"

_Static_assert(STR_CHIP_HOLD_BACK - STR_CHIP_VOL == FOOTER_KEY_HOLD_BACK - FOOTER_KEY_VOL,
               "key chips follow the key order");

static const char SEPARATOR[] = "\xC2\xB7";

typedef struct FooterInk {
    Color key;
    Color verb;
} FooterInk;

typedef struct FooterRun {
    Rect area;
    int inset;
    int line;
    bool stacked;
    FooterInk ink;
} FooterRun;

typedef struct FooterLine {
    int first;
    int end;
    int width;
} FooterLine;

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

const char *footer_hints_key_name(FooterKey key)
{
    return i18n_text((StringId)(STR_CHIP_VOL + key));
}

static int chip_height(const Theme *theme)
{
    return font_line_height(theme->fonts.small);
}

static int chip_width(const Theme *theme, const FooterHint *hint)
{
    return text_width(theme->fonts.small, footer_hints_key_name((FooterKey)hint->key)) +
           2 * FOOTER_CHIP_PADDING;
}

static int verb_width(const Theme *theme, const FooterHint *hint)
{
    return text_width(theme->fonts.small, i18n_text((StringId)hint->verb));
}

static int hint_width(const Theme *theme, const FooterHint *hint, bool stacked)
{
    int chip = chip_width(theme, hint);
    int verb = verb_width(theme, hint);
    return stacked ? max_of(chip, verb) : chip + FOOTER_CHIP_GAP + verb;
}

static int separator_room(const Theme *theme, bool stacked)
{
    return stacked ? theme->metrics.padding
                   : 2 * FOOTER_HINT_GAP + text_width(theme->fonts.small, SEPARATOR);
}

static int line_height(const Theme *theme, bool stacked)
{
    return (stacked ? 2 : 1) * chip_height(theme) + (stacked ? 2 : 1) * FOOTER_LINE_SPACE;
}

static FooterLine next_line(const Theme *theme, FooterHints hints, int first, bool stacked,
                            int width)
{
    FooterLine line = {first, first, 0};
    while (line.end < hints.count) {
        int add = hint_width(theme, &hints.items[line.end], stacked);
        add += line.end > first ? separator_room(theme, stacked) : 0;
        if (line.end > first && line.width + add > width) {
            break;
        }
        line.width += add;
        line.end++;
    }
    return line;
}

static int count_lines(const Theme *theme, FooterHints hints, bool stacked, int width)
{
    int lines = 0;
    for (int first = 0; first < hints.count; lines++) {
        first = next_line(theme, hints, first, stacked, width).end;
    }
    return lines;
}

int footer_hints_lines(const Theme *theme, FooterHints hints, int width)
{
    return count_lines(theme, hints, theme_is_portrait(theme), width);
}

int footer_hints_height(const Theme *theme, FooterHints hints)
{
    int width = theme->metrics.screen_width - 2 * theme->metrics.padding;
    bool stacked = theme_is_portrait(theme);
    return count_lines(theme, hints, stacked, width) * line_height(theme, stacked);
}

Rect footer_hints_area(const Theme *theme, Rect content, FooterHints hints)
{
    int height = min_of(content.height, footer_hints_height(theme, hints));
    return (Rect){content.x, content.y + content.height - height, content.width, height};
}

static void draw_chip(Canvas *canvas, const Theme *theme, Rect box, const char *name, FooterInk ink)
{
    canvas_fill_rect(canvas, (Rect){box.x, box.y, box.width, 1}, ink.verb);
    canvas_fill_rect(canvas, (Rect){box.x, box.y + box.height - 1, box.width, 1}, ink.verb);
    canvas_fill_rect(canvas, (Rect){box.x, box.y, 1, box.height}, ink.verb);
    canvas_fill_rect(canvas, (Rect){box.x + box.width - 1, box.y, 1, box.height}, ink.verb);
    Rect text = {box.x + FOOTER_CHIP_PADDING, box.y, box.width - 2 * FOOTER_CHIP_PADDING,
                 box.height};
    label_draw(canvas, (TextStyle){theme->fonts.small, ink.key, false}, (Label){text, name});
}

static void draw_hint(Canvas *canvas, const Theme *theme, const FooterRun *run,
                      const FooterHint *hint, Rect cell)
{
    int chip = min_of(chip_width(theme, hint), cell.width);
    int high = chip_height(theme);
    const char *verb = i18n_text((StringId)hint->verb);
    Rect box = {cell.x, cell.y, chip, high};
    Rect words = {cell.x + chip + FOOTER_CHIP_GAP, cell.y, cell.width - chip - FOOTER_CHIP_GAP,
                  high};
    if (run->stacked) {
        box.x += (cell.width - chip) / 2;
        int width = min_of(verb_width(theme, hint), cell.width);
        words = (Rect){cell.x + (cell.width - width) / 2, cell.y + high, width, high};
    }
    draw_chip(canvas, theme, box, footer_hints_key_name((FooterKey)hint->key), run->ink);
    label_draw(canvas, (TextStyle){theme->fonts.small, run->ink.verb, false}, (Label){words, verb});
}

static int line_start(const FooterRun *run, FooterHints hints, FooterLine line, int width)
{
    int used = min_of(line.width, width);
    int left = run->area.x + run->inset;
    if (run->stacked) {
        return left + (width - used) / 2;
    }
    return hints.align == FOOTER_ALIGN_END ? left + width - used : left;
}

static void draw_line(Canvas *canvas, const Theme *theme, const FooterRun *run, FooterHints hints,
                      FooterLine line, int top)
{
    int width = run->area.width - 2 * run->inset;
    int right = run->area.x + run->inset + width;
    int x = line_start(run, hints, line, width);
    TextStyle dot = {theme->fonts.small, run->ink.verb, false};
    for (int index = line.first; index < line.end; index++) {
        if (index > line.first) {
            int room = separator_room(theme, run->stacked);
            Rect mark = {x + FOOTER_HINT_GAP, top, room - 2 * FOOTER_HINT_GAP, chip_height(theme)};
            if (!run->stacked) {
                label_draw(canvas, dot, (Label){mark, SEPARATOR});
            }
            x += room;
        }
        const FooterHint *hint = &hints.items[index];
        int cell = min_of(hint_width(theme, hint, run->stacked), right - x);
        draw_hint(canvas, theme, run, hint, (Rect){x, top, cell, run->area.height});
        x += cell;
    }
}

static void draw_lines(Canvas *canvas, const Theme *theme, const FooterRun *run, FooterHints hints)
{
    int width = run->area.width - 2 * run->inset;
    int height = run->line;
    int content = (run->stacked ? 2 : 1) * chip_height(theme);
    Canvas clipped = canvas_clipped(canvas, run->area);
    int y = run->area.y;
    for (int first = 0; first < hints.count; y += height) {
        FooterLine line = next_line(theme, hints, first, run->stacked, width);
        draw_line(&clipped, theme, run, hints, line, y + (height - content) / 2);
        first = line.end;
    }
}

void footer_hints_draw(Canvas *canvas, const Theme *theme, Rect area, FooterHints hints)
{
    if (hints.count == 0 || rect_is_empty(area)) {
        return;
    }
    const ThemeColors *colors = &theme->colors;
    canvas_fill_rect(canvas, area, colors->header_background);
    canvas_fill_rect(canvas, (Rect){area.x, area.y, area.width, 1}, colors->divider);
    FooterInk ink = {colors->header_text, color_blend(colors->header_background,
                                                      colors->header_text, FOOTER_VERB_ALPHA)};
    bool stacked = theme_is_portrait(theme);
    FooterRun run = {area, theme->metrics.padding, line_height(theme, stacked), stacked, ink};
    draw_lines(canvas, theme, &run, hints);
}

void footer_hints_draw_inline(Canvas *canvas, const Theme *theme, Rect area, FooterHints hints)
{
    FooterRun run = {
        area, 0, area.height, false, {theme->colors.text, theme->colors.text_secondary}};
    draw_lines(canvas, theme, &run, hints);
}
