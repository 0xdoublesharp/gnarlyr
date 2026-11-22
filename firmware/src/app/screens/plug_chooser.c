#include "plug_chooser.h"

#include "core/deadline.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"

enum {
    MILLISECONDS_PER_SECOND = 1000,
    LINE_ROWS = 3,
    NAME_ROWS = 2,
    HERO_THIRDS = 1,
    THIRDS = 3
};

static const char CHECK_MARK[] = "\xE2\x9C\x93";
static const FooterHint ALWAYS_HINT[] = {{FOOTER_KEY_HOLD_PLAY, STR_HINT_ALWAYS}};
static const FooterHints ALWAYS_FOOTER = {ALWAYS_HINT, 1, FOOTER_ALIGN_END};

PlugMode plug_chooser_mode(const PlugChooser *chooser, int row)
{
    return (PlugMode)chooser->choices.modes[row];
}

static PlugMode selected_mode(const PlugChooser *chooser)
{
    return plug_chooser_mode(chooser, chooser->list.selected);
}

const char *plug_chooser_line(const PlugChooser *chooser)
{
    return i18n_text(plug_mode_line(selected_mode(chooser), chooser->config.state.battery));
}

int plug_chooser_footer_rows(const PlugChooser *chooser)
{
    PlugPlace place = chooser->config.state.place;
    return place == PLUG_PLACE_SETTINGS ? 0 : place == PLUG_PLACE_PROMPT ? 2 : 1;
}

static int row_count(const void *context)
{
    const PlugChooser *chooser = context;
    return chooser->choices.count;
}

static const char *row_label(const void *context, int index)
{
    const PlugChooser *chooser = context;
    PlugMode mode = plug_chooser_mode(chooser, index);
    return i18n_text(theme_is_portrait(chooser->config.theme) ? plug_mode_short_name(mode)
                                                              : plug_mode_name(mode));
}

static const char *row_mark(const void *context, int index)
{
    const PlugChooser *chooser = context;
    return chooser->choices.marked == index ? CHECK_MARK : NULL;
}

static IconId row_icon(const void *context, int index)
{
    return plug_mode_icon(plug_chooser_mode(context, index));
}

static void count_down(PlugChooser *chooser, uint32_t now_ms)
{
    chooser->expired = chooser->timed && deadline_reached(now_ms, chooser->until_ms);
    uint32_t left = chooser->expired ? 0
                                     : (chooser->until_ms - now_ms + MILLISECONDS_PER_SECOND - 1) /
                                           MILLISECONDS_PER_SECOND;
    if (!chooser->timed || left == chooser->seconds) {
        return;
    }
    chooser->seconds = left;
    chooser->redraw = true;
    StringBuilder text = string_builder_make(chooser->countdown, sizeof chooser->countdown);
    i18n_format_counts(&text, STR_PLUG_COUNTDOWN, left, 0);
}

static ScreenAction choose_row(PlugChooser *chooser, int index, bool always)
{
    chooser->config.choose(chooser->config.context, plug_chooser_mode(chooser, index), always);
    return screen_action_pop();
}

static ScreenAction select_row(void *context, int index)
{
    return choose_row(context, index, false);
}

static ScreenAction hold_row(void *context, int index)
{
    PlugChooser *chooser = context;
    if (chooser->config.state.place == PLUG_PLACE_SETTINGS) {
        return screen_action_none();
    }
    return choose_row(chooser, index, true);
}

static void enter(void *context, uint32_t now_ms)
{
    PlugChooser *chooser = context;
    chooser->now_ms = now_ms;
    chooser->until_ms = now_ms + PLUG_PROMPT_MS;
    chooser->seconds = 0;
    list_view_select(&chooser->list, chooser->choices.cursor, now_ms);
    count_down(chooser, now_ms);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    PlugChooser *chooser = context;
    chooser->until_ms = now_ms + PLUG_PROMPT_MS;
    count_down(chooser, now_ms);
    ListSelection selection = {.select = select_row, .hold = hold_row, .context = chooser};
    return list_navigation_handle(&chooser->list, event, now_ms, selection);
}

static void update(void *context, uint32_t now_ms)
{
    PlugChooser *chooser = context;
    chooser->now_ms = now_ms;
    count_down(chooser, now_ms);
}

static bool inline_hint(const Theme *theme, int footer_rows)
{
    return theme_is_portrait(theme) && footer_rows > 1;
}

static int band_height(const Theme *theme, int footer_rows)
{
    if (inline_hint(theme, footer_rows)) {
        return font_line_height(theme->fonts.small) + theme->metrics.padding / 2;
    }
    return footer_rows > 0 ? footer_hints_height(theme, ALWAYS_FOOTER) : 0;
}

static Rect band_area(const Theme *theme, Rect content, int footer_rows)
{
    int band = band_height(theme, footer_rows);
    return (Rect){content.x, content.y + content.height - band, content.width, band};
}

static PlugChooserLayout landscape(const Theme *theme, Rect content, int footer_rows)
{
    int pad = theme->metrics.padding;
    Rect band = band_area(theme, content, footer_rows);
    int text = LINE_ROWS * font_line_height(theme->fonts.small);
    int hero = content.width * HERO_THIRDS / THIRDS;
    int body = band.y - content.y;
    Rect line = {content.x + pad, content.y + body - text - pad / 2, hero - 2 * pad, text};
    return (PlugChooserLayout){
        .art = {line.x, content.y + pad, line.width, line.y - content.y - pad - pad / 2},
        .name = {line.x, line.y, line.width, 0},
        .line = line,
        .footer = band,
        .count = {band.x + pad, band.y, band.width / 2 - pad, footer_rows > 1 ? band.height : 0},
        .list = {content.x + hero, content.y, content.width - hero, body},
    };
}

static PlugChooserLayout portrait(const Theme *theme, Rect content, int footer_rows, int rows)
{
    int pad = theme->metrics.padding;
    int width = content.width - 2 * pad;
    Rect band = band_area(theme, content, footer_rows);
    int small = font_line_height(theme->fonts.small);
    Rect count = {content.x + pad, band.y - (footer_rows > 1 ? small + pad / 2 : 0), width,
                  footer_rows > 1 ? small : 0};
    int list = rows * theme->metrics.row_height;
    Rect list_area = {content.x, count.y - pad / 2 - list, content.width, list};
    int text = LINE_ROWS * small;
    int name = NAME_ROWS * font_line_height(theme->fonts.title);
    Rect line = {count.x, list_area.y - pad / 2 - text, width, text};
    Rect named = {count.x, line.y - name, width, name};
    return (PlugChooserLayout){
        .art = {count.x, content.y + pad, width, named.y - content.y - pad - pad / 2},
        .name = named,
        .line = line,
        .footer = band,
        .count = count,
        .list = list_area,
    };
}

PlugChooserLayout plug_chooser_layout(const Theme *theme, int footer_rows, int rows)
{
    Rect content = theme_content_area(theme);
    if (theme_is_portrait(theme)) {
        return portrait(theme, content, footer_rows, rows);
    }
    return landscape(theme, content, footer_rows);
}

static void draw_footer(const PlugChooser *chooser, Canvas *canvas, PlugChooserLayout layout)
{
    const Theme *theme = chooser->config.theme;
    if (inline_hint(theme, plug_chooser_footer_rows(chooser))) {
        Rect line = {layout.count.x, layout.footer.y, layout.count.width, layout.count.height};
        footer_hints_draw_inline(canvas, theme, line, ALWAYS_FOOTER);
    } else {
        footer_hints_draw(canvas, theme, layout.footer, ALWAYS_FOOTER);
    }
    if (chooser->timed) {
        label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.accent, false},
                   (Label){layout.count, chooser->countdown});
    }
}

static PlugChooserLayout chooser_layout(const PlugChooser *chooser)
{
    return plug_chooser_layout(chooser->config.theme, plug_chooser_footer_rows(chooser),
                               chooser->choices.count);
}

static void draw(void *context, Canvas *canvas)
{
    const PlugChooser *chooser = context;
    const Theme *theme = chooser->config.theme;
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    header_draw(canvas, theme, chooser->config.title);
    PlugChooserLayout layout = chooser_layout(chooser);
    plug_art_draw(canvas, theme, layout.art, selected_mode(chooser));
    if (layout.name.height > 0) {
        label_draw_wrapped(canvas, (TextStyle){theme->fonts.title, theme->colors.text, false},
                           (Label){layout.name, i18n_text(plug_mode_name(selected_mode(chooser)))});
    }
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
                       (Label){layout.line, plug_chooser_line(chooser)});
    draw_footer(chooser, canvas, layout);
    list_view_draw(&chooser->list, canvas, layout.list, chooser->now_ms);
}

Screen *plug_chooser_init(PlugChooser *chooser, PlugChooserConfig config)
{
    chooser->config = config;
    chooser->choices = plug_modes_choices(config.state);
    chooser->now_ms = 0;
    chooser->timed = config.state.place == PLUG_PLACE_PROMPT;
    chooser->expired = false;
    chooser->redraw = false;
    chooser->countdown[0] = '\0';
    chooser->model = (ListModel){.context = chooser,
                                 .count = row_count,
                                 .label = row_label,
                                 .detail = row_mark,
                                 .icon = row_icon};
    list_view_init(&chooser->list, &chooser->model, config.theme, 0);
    PlugChooserLayout layout = chooser_layout(chooser);
    list_view_fit_rows(&chooser->list, list_view_rows_in(&chooser->list, layout.list.height));
    chooser->screen = (Screen){
        .title = config.title,
        .context = chooser,
        .paints_every_pixel = true,
        .keys = config.state.place != PLUG_PLACE_SETTINGS ? SCREEN_KEYS_ALL : SCREEN_KEYS_SHARED,
        .stays_on_wake = true,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw};
    return &chooser->screen;
}
