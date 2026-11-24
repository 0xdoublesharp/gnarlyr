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
    int width;
} FooterCell;

typedef struct FooterPlan {
    FooterCell cells[FOOTER_MEASURED];
    int count;
    int gap;
    bool stacked;
} FooterPlan;

typedef struct FooterRun {
    const Font *font;
    Rect area;
    int inset;
    int line;
    FooterInk ink;
} FooterRun;

typedef struct FooterDraw {
    Canvas *canvas;
    const FooterRun *run;
    const FooterPlan *plan;
    FooterHints hints;
} FooterDraw;

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
    return i18n_text((StringId)((int)STR_CHIP_VOL + (int)key));
}

static void measure(const Font *font, const FooterHint *items, int padding, FooterPlan *plan)
{
    plan->gap = plan->stacked ? padding : 2 * FOOTER_HINT_GAP + text_width(font, SEPARATOR);
    for (int index = 0; index < plan->count; index++) {
        const FooterHint *hint = &items[index];
        plan->cells[index].chip =
            (int16_t)(text_width(font, footer_hints_key_name((FooterKey)hint->key)) +
                      2 * FOOTER_CHIP_PADDING);
        plan->cells[index].verb = (int16_t)text_width(font, i18n_text((StringId)hint->verb));
        FooterCell *cell = &plan->cells[index];
        cell->width = plan->stacked ? max_of(cell->chip, cell->verb)
                                    : cell->chip + FOOTER_CHIP_GAP + cell->verb;
    }
}

__attribute__((noinline)) static int line_end(const FooterPlan *plan, int first, int width,
                                              int *used)
{
    int total = plan->cells[first].width;
    int end = first + 1;
    while (end < plan->count && total + plan->gap + plan->cells[end].width <= width) {
        total += plan->gap + plan->cells[end].width;
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
    FooterPlan plan = {0};
    plan.count = min_of(hints.count, FOOTER_MEASURED);
    plan.stacked = theme_is_portrait(theme);
    measure(theme->fonts.small, hints.items, theme->metrics.padding, &plan);
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

__attribute__((noinline)) static void draw_cell(const FooterDraw *draw, Rect cell, int index)
{
    const FooterRun *run = draw->run;
    const FooterHint *hint = &draw->hints.items[index];
    FooterCell size = draw->plan->cells[index];
    TextStyle style = {run->font, run->ink.verb, false};
    int high = font_line_height(style.font);
    int chip = min_of(size.chip, cell.width);
    Rect box = {cell.x, cell.y, chip, high};
    Rect words = {cell.x + chip + FOOTER_CHIP_GAP, cell.y, cell.width - chip - FOOTER_CHIP_GAP,
                  high};
    if (draw->plan->stacked) {
        box.x += (cell.width - chip) / 2;
        words = (Rect){cell.x + max_of(0, (cell.width - size.verb) / 2), cell.y + high, cell.width,
                       high};
    }
    canvas_outline_rect(draw->canvas, box, run->ink.verb);
    label_draw(draw->canvas, style, (Label){words, i18n_text((StringId)hint->verb)});
    style.color = run->ink.key;
    box.x += FOOTER_CHIP_PADDING;
    box.width -= 2 * FOOTER_CHIP_PADDING;
    label_draw(draw->canvas, style, (Label){box, footer_hints_key_name((FooterKey)hint->key)});
}

static void footer_service(void (*service)(void))
{
    if (service != NULL) {
        service();
    }
}

static void draw_hint_row(const FooterDraw *draw, Rect row, int first, int end)
{
    const FooterPlan *plan = draw->plan;
    Rect clip = draw->canvas->clip;
    if (row.y >= clip.y + clip.height || row.y + row.height <= clip.y) {
        return;
    }
    int right = clip.x + clip.width;
    for (int index = first; index < end; index++) {
        Rect cell = {row.x, row.y, min_of(plan->cells[index].width, row.width), row.height};
        if (cell.x < right && cell.x + cell.width > clip.x) {
            draw_cell(draw, cell, index);
            footer_service(draw->hints.service);
        }
        row.x += cell.width;
        if (index + 1 < end && !plan->stacked) {
            Rect mark = {row.x + FOOTER_HINT_GAP, row.y, plan->gap - 2 * FOOTER_HINT_GAP,
                         row.height};
            if (mark.x < right && mark.x + mark.width > clip.x) {
                TextStyle dot = {draw->run->font, draw->run->ink.verb, false};
                label_draw(draw->canvas, dot, (Label){mark, SEPARATOR});
                footer_service(draw->hints.service);
            }
        }
        row.x += plan->gap;
    }
}

static void draw_lines(Canvas *canvas, const FooterRun *run, FooterHints hints, int padding)
{
    Canvas clipped = canvas_clipped(canvas, run->area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    int font_height = font_line_height(run->font);
    FooterPlan plan = {0};
    plan.count = min_of(hints.count, FOOTER_MEASURED);
    plan.stacked = run->line > 2 * font_height;
    measure(run->font, hints.items, padding, &plan);
    footer_service(hints.service);
    FooterDraw draw = {&clipped, run, &plan, hints};
    int width = run->area.width - 2 * run->inset;
    int high = (plan.stacked ? 2 : 1) * font_height;
    int left = run->area.x + run->inset;
    for (int first = 0, y = run->area.y; first < plan.count; y += run->line) {
        int used;
        int end = line_end(&plan, first, width, &used);
        int spare = width - min_of(used, width);
        int shift = plan.stacked ? spare / 2 : hints.align == FOOTER_ALIGN_END ? spare : 0;
        Rect row = {left + shift, y + (run->line - high) / 2, width - shift, high};
        draw_hint_row(&draw, row, first, end);
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
    FooterRun run = {theme->fonts.small, area, theme->metrics.padding,
                     line_height(theme, theme_is_portrait(theme)), ink};
    draw_lines(canvas, &run, hints, theme->metrics.padding);
}

void footer_hints_draw_inline(Canvas *canvas, const Theme *theme, Rect area, FooterHints hints)
{
    FooterRun run = {theme->fonts.small,
                     area,
                     0,
                     area.height,
                     {theme->colors.text, theme->colors.text_secondary}};
    draw_lines(canvas, &run, hints, theme->metrics.padding);
}
