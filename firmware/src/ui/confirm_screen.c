#include "ui/confirm_screen.h"

#include <stddef.h>

#include "gfx/text.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

static const FooterHint BACK_CANCELS[] = {{FOOTER_KEY_BACK, STR_CANCEL}};
static const FooterHints FOOTER = {BACK_CANCELS, 1, FOOTER_ALIGN_END};

typedef struct ConfirmLayout {
    Rect notes;
    Rect rows;
    Rect footer;
    int subject;
} ConfirmLayout;

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

static int choice_count(const void *context)
{
    const ConfirmScreen *confirm = context;
    return confirm->config.choice_count;
}

static const char *choice_label(const void *context, int index)
{
    const ConfirmScreen *confirm = context;
    return confirm->config.choices[index];
}

static ListRowRole choice_role(const void *context, int index)
{
    const ConfirmScreen *confirm = context;
    return index >= confirm->config.first_danger ? LIST_ROW_DANGER : LIST_ROW_PLAIN;
}

static int lead_width(const Theme *theme, const char *label)
{
    return label != NULL ? text_width(theme->fonts.small, label) + theme->metrics.padding / 2 : 0;
}

static int lines_height(const Font *font, const char *text, int width)
{
    return text != NULL ? label_wrap_count(font, text, width) * font_line_height(font) : 0;
}

static int sections_height(const ConfirmScreen *confirm, int width)
{
    const Theme *theme = confirm->config.theme;
    int height = 0;
    for (int index = 0; index < CONFIRM_SECTIONS; index++) {
        const ConfirmSection *section = &confirm->config.sections[index];
        height += lines_height(theme->fonts.small, section->text,
                               width - lead_width(theme, section->label));
    }
    return height;
}

static ConfirmLayout layout_of(ConfirmScreen *confirm)
{
    const Theme *theme = confirm->config.theme;
    int pad = theme->metrics.padding;
    Rect content = theme_content_area(theme);
    Rect footer = footer_hints_area(theme, content, FOOTER);
    int body = footer.y - content.y;
    int width = content.width - 2 * pad;
    int sections = sections_height(confirm, width);
    int subject = lines_height(theme->fonts.body, confirm->config.subject, width);
    int room = body - sections - pad;
    int rows =
        max_of(1, min_of(confirm->config.choice_count, list_view_rows_in(&confirm->list, room)));
    int rows_height = rows * list_view_row_height(&confirm->list);
    Rect notes = {content.x + pad, content.y + pad / 2, width, body - rows_height - pad / 2};
    int line = font_line_height(theme->fonts.body);
    int lines = max_of(0, notes.height - sections) / line;
    return (ConfirmLayout){
        .notes = notes,
        .rows = {content.x, footer.y - rows_height, content.width, rows_height},
        .footer = footer,
        .subject = min_of(subject, lines * line),
    };
}

static int draw_section(Canvas *canvas, const Theme *theme, const ConfirmSection *section,
                        Rect area)
{
    if (section->text == NULL) {
        return 0;
    }
    int lead = lead_width(theme, section->label);
    int height = lines_height(theme->fonts.small, section->text, area.width - lead);
    if (section->label != NULL) {
        label_draw(
            canvas, (TextStyle){theme->fonts.small, theme->colors.accent, false},
            (Label){{area.x, area.y, lead, font_line_height(theme->fonts.small)}, section->label});
    }
    label_draw_paragraph(
        canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
        (Label){{area.x + lead, area.y, area.width - lead, height}, section->text});
    return height;
}

static void draw_notes(Canvas *canvas, const ConfirmScreen *confirm, ConfirmLayout layout)
{
    const Theme *theme = confirm->config.theme;
    Rect area = layout.notes;
    if (confirm->config.subject != NULL && layout.subject > 0) {
        label_draw_paragraph(
            canvas, (TextStyle){theme->fonts.body, theme->colors.text, false},
            (Label){{area.x, area.y, area.width, layout.subject}, confirm->config.subject});
        area.y += layout.subject;
    }
    for (int index = 0; index < CONFIRM_SECTIONS; index++) {
        area.y += draw_section(canvas, theme, &confirm->config.sections[index], area);
    }
}

static void draw(void *context, Canvas *canvas)
{
    ConfirmScreen *confirm = context;
    const Theme *theme = confirm->config.theme;
    ConfirmLayout layout = layout_of(confirm);
    canvas_fill_rect(canvas, theme_content_area(theme), theme->colors.background);
    header_draw(canvas, theme, confirm->config.title);
    draw_notes(canvas, confirm, layout);
    list_view_fit_rows(&confirm->list, layout.rows.height / list_view_row_height(&confirm->list));
    list_view_draw(&confirm->list, canvas, layout.rows, confirm->now_ms);
    footer_hints_draw(canvas, theme, layout.footer, FOOTER);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    ConfirmScreen *confirm = context;
    confirm->now_ms = now_ms;
    NavigationIntent intent = navigation_from_key(event).intent;
    if (intent == NAVIGATION_SELECT || intent == NAVIGATION_BACK) {
        int choice = intent == NAVIGATION_BACK ? CONFIRM_CANCEL : confirm->list.selected;
        return confirm->config.choose(confirm->config.context, choice);
    }
    if (intent == NAVIGATION_OPTIONS) {
        return screen_action_none();
    }
    return list_navigation_handle(&confirm->list, event, now_ms, (ListSelection){0});
}

static void enter(void *context, uint32_t now_ms)
{
    ConfirmScreen *confirm = context;
    confirm->now_ms = now_ms;
    list_view_restart_marquee(&confirm->list, now_ms);
}

static void update(void *context, uint32_t now_ms)
{
    ConfirmScreen *confirm = context;
    confirm->now_ms = now_ms;
}

Screen *confirm_screen_init(ConfirmScreen *confirm, ConfirmConfig config)
{
    *confirm = (ConfirmScreen){.config = config};
    confirm->model = (ListModel){
        .context = confirm, .count = choice_count, .label = choice_label, .role = choice_role};
    list_view_init(&confirm->list, &confirm->model, config.theme, 0);
    confirm->screen = (Screen){
        .title = config.title,
        .context = confirm,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &confirm->screen;
}
