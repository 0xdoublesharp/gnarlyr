#include "ui/footer_hints.h"

#include <stdbool.h>

#include "gfx/text.h"
#include "ui/label.h"

_Static_assert(STR_CHIP_HOLD_BACK - STR_CHIP_VOL == FOOTER_KEY_HOLD_BACK - FOOTER_KEY_VOL,
               "key chips follow the key order");

static const char SEPARATOR[] = "\xC2\xB7";

enum {
    FOOTER_MEASURED = 8
};

typedef struct FooterInk {
    Color key;
    Color verb;
} FooterInk;

typedef struct FooterCell {
    int16_t chip;
    int16_t verb;
} FooterCell;

typedef struct FooterPlan {
    FooterCell cells[FOOTER_MEASURED];
    int count;
    int gap;
    bool stacked;
} FooterPlan;

typedef struct FooterRun {
    Rect area;
    int inset;
    int line;
    FooterInk ink;
} FooterRun;

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

static void measure(const Theme *theme, FooterHints hints, bool stacked, FooterPlan *plan)
{
    const Font *font = theme->fonts.small;
    plan->count = min_of(hints.count, FOOTER_MEASURED);
    plan->stacked = stacked;
    plan->gap =
        stacked ? theme->metrics.padding : 2 * FOOTER_HINT_GAP + text_width(font, SEPARATOR);
    for (int index = 0; index < plan->count; index++) {
        const FooterHint *hint = &hints.items[index];
        plan->cells[index].chip =
            (int16_t)(text_width(font, footer_hints_key_name((FooterKey)hint->key)) +
                      2 * FOOTER_CHIP_PADDING);
        plan->cells[index].verb = (int16_t)text_width(font, i18n_text((StringId)hint->verb));
    }
}

static int cell_width(const FooterPlan *plan, int index)
{
    FooterCell cell = plan->cells[index];
    return plan->stacked ? max_of(cell.chip, cell.verb) : cell.chip + FOOTER_CHIP_GAP + cell.verb;
}

static int line_end(const FooterPlan *plan, int first, int width, int *used)
{
    int total = cell_width(plan, first);
    int end = first + 1;
    while (end < plan->count && total + plan->gap + cell_width(plan, end) <= width) {
        total += plan->gap + cell_width(plan, end);
        end++;
    }
    *used = total;
    return end;
}

static int count_lines(const FooterPlan *plan, int width)
{
    int lines = 0;
    int used = 0;
    for (int first = 0; first < plan->count; lines++) {
        first = line_end(plan, first, width, &used);
    }
    return lines;
}

static int line_height(const Theme *theme, bool stacked)
{
    return (stacked ? 2 : 1) * (font_line_height(theme->fonts.small) + FOOTER_LINE_SPACE);
}

int footer_hints_lines(const Theme *theme, FooterHints hints, int width)
{
    FooterPlan plan;
    measure(theme, hints, theme_is_portrait(theme), &plan);
    return count_lines(&plan, width);
}

int footer_hints_height(const Theme *theme, FooterHints hints)
{
    int width = theme->metrics.screen_width - 2 * theme->metrics.padding;
    return footer_hints_lines(theme, hints, width) * line_height(theme, theme_is_portrait(theme));
}

Rect footer_hints_area(const Theme *theme, Rect content, FooterHints hints)
{
    int height = min_of(content.height, footer_hints_height(theme, hints));
    return (Rect){content.x, content.y + content.height - height, content.width, height};
}

static void draw_frame(Canvas *canvas, Rect box, Color color)
{
    for (int side = 0; side < 4; side++) {
        Rect edge = box;
        edge.x += side == 1 ? box.width - 1 : 0;
        edge.y += side == 3 ? box.height - 1 : 0;
        edge.width = side < 2 ? 1 : box.width;
        edge.height = side < 2 ? box.height : 1;
        canvas_fill_rect(canvas, edge, color);
    }
}

static void draw_cell(Canvas *canvas, const Theme *theme, const FooterRun *run, Rect cell,
                      const FooterHint *hint, FooterCell size, bool stacked)
{
    TextStyle style = {theme->fonts.small, run->ink.verb, false};
    int high = font_line_height(style.font);
    int chip = min_of(size.chip, cell.width);
    Rect box = {cell.x, cell.y, chip, high};
    Rect words = {cell.x + chip + FOOTER_CHIP_GAP, cell.y, cell.width - chip - FOOTER_CHIP_GAP,
                  high};
    if (stacked) {
        box.x += (cell.width - chip) / 2;
        words = (Rect){cell.x + max_of(0, (cell.width - size.verb) / 2), cell.y + high, cell.width,
                       high};
    }
    draw_frame(canvas, box, run->ink.verb);
    label_draw(canvas, style, (Label){words, i18n_text((StringId)hint->verb)});
    style.color = run->ink.key;
    box.x += FOOTER_CHIP_PADDING;
    box.width -= 2 * FOOTER_CHIP_PADDING;
    label_draw(canvas, style, (Label){box, footer_hints_key_name((FooterKey)hint->key)});
}

static void draw_lines(Canvas *canvas, const Theme *theme, const FooterRun *run, FooterHints hints)
{
    FooterPlan plan;
    measure(theme, hints, run->line > 2 * font_line_height(theme->fonts.small), &plan);
    int width = run->area.width - 2 * run->inset;
    int left = run->area.x + run->inset;
    int high = (plan.stacked ? 2 : 1) * font_line_height(theme->fonts.small);
    TextStyle dot = {theme->fonts.small, run->ink.verb, false};
    Canvas clipped = canvas_clipped(canvas, run->area);
    int used = 0;
    for (int first = 0, y = run->area.y; first < plan.count; y += run->line) {
        int end = line_end(&plan, first, width, &used);
        int spare = width - min_of(used, width);
        int x = left + (plan.stacked ? spare / 2 : hints.align == FOOTER_ALIGN_END ? spare : 0);
        int top = y + (run->line - high) / 2;
        for (int index = first; index < end; index++) {
            if (index > first && !plan.stacked) {
                Rect mark = {x + FOOTER_HINT_GAP, top, plan.gap - 2 * FOOTER_HINT_GAP, high};
                label_draw(&clipped, dot, (Label){mark, SEPARATOR});
            }
            x += index > first ? plan.gap : 0;
            Rect cell = {x, top, min_of(cell_width(&plan, index), left + width - x), high};
            draw_cell(&clipped, theme, run, cell, &hints.items[index], plan.cells[index],
                      plan.stacked);
            x += cell.width;
        }
        first = end;
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
    FooterRun run = {area, theme->metrics.padding, line_height(theme, theme_is_portrait(theme)),
                     ink};
    draw_lines(canvas, theme, &run, hints);
}

void footer_hints_draw_inline(Canvas *canvas, const Theme *theme, Rect area, FooterHints hints)
{
    FooterRun run = {area, 0, area.height, {theme->colors.text, theme->colors.text_secondary}};
    draw_lines(canvas, theme, &run, hints);
}
