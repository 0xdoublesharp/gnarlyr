#include "charge_screen.h"

#include "core/i18n.h"
#include "core/utf8.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    FULL_PERCENT = 100,
    READOUT_SCALE = 2,
    TEXT_CAPACITY = 64,
    BATTERY_CELLS = 4
};

typedef struct ChargeArt {
    Canvas *canvas;
    const Theme *theme;
    HalBattery battery;
    ChargeScreenLayout layout;
    uint32_t phase;
} ChargeArt;

static int larger(int first, int second)
{
    return first > second ? first : second;
}

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static int shown_percent(HalBattery battery)
{
    return smaller(battery.percent, FULL_PERCENT);
}

static bool flowing(const ChargeArt *art)
{
    return art->battery.usb_power && art->battery.charging;
}

ChargeScreenLayout charge_screen_art_layout(const Theme *theme)
{
    int pad = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * pad;
    int line = font_line_height(theme->fonts.title);
    int small = font_line_height(theme->fonts.small);
    const char *hint = i18n_text(STR_CHARGE_MODE_HINT);
    int detail = label_wrap_count(theme->fonts.small, hint, width) * small;
    ChargeScreenLayout layout = {
        .header = {pad, 0, width, theme->metrics.header_height},
        .detail = {pad, theme->metrics.screen_height - pad - detail, width, detail}};
    int first = theme->metrics.header_height + pad;
    int room = layout.detail.y - pad - first;
    if (theme_is_portrait(theme)) {
        int battery_width = width - 2 * pad;
        int battery_height = battery_width / 2;
        int group = battery_height + READOUT_SCALE * line + line + small + 2 * pad + pad / 2;
        int top = first + (room - group) / 2;
        layout.battery = (Rect){2 * pad, top, battery_width, battery_height};
        layout.percent = (Rect){pad, top + battery_height + pad, width, READOUT_SCALE * line};
        layout.status = (Rect){pad, layout.percent.y + layout.percent.height + pad, width, line};
        layout.voltage = (Rect){pad, layout.status.y + line + pad / 2, width, small};
        return layout;
    }
    int side = (width - 2 * pad) / 2;
    int height = smaller((side - pad) / 2, room - 2 * pad);
    layout.battery =
        (Rect){pad + (side - 2 * height) / 2, first + (room - height) / 2, 2 * height, height};
    int top = first + (room - READOUT_SCALE * line - 2 * small - pad / 2) / 2;
    int right = pad + side + 2 * pad;
    layout.percent = (Rect){right, top, side, READOUT_SCALE * line};
    layout.status = (Rect){right, top + READOUT_SCALE * line, side, small};
    layout.voltage = (Rect){right, layout.status.y + small + pad / 2, side, small};
    return layout;
}

static void draw_label(const ChargeArt *art, Rect area, const Font *font, const char *text)
{
    area = label_centered_area(font, area, text);
    label_draw(art->canvas, (TextStyle){font, art->theme->colors.text}, (Label){area, text});
}

static void draw_header(const ChargeArt *art)
{
    const Theme *theme = art->theme;
    Rect name = art->layout.header;
    Rect title = name;
    int line = font_line_height(theme->fonts.header);
    if (theme_is_portrait(theme)) {
        name.height = line;
        title.y += line;
        title.height -= line;
    } else {
        const char *text = i18n_text(STR_POWER_TITLE);
        title.width = text_width(theme->fonts.small, text);
        title.x += name.width - title.width;
    }
    label_draw(art->canvas, (TextStyle){theme->fonts.header, theme->colors.text},
               (Label){name, FIRMWARE_SHORT_NAME});
    label_draw(art->canvas, (TextStyle){theme->fonts.small, theme->colors.accent},
               (Label){title, i18n_text(STR_POWER_TITLE)});
    canvas_fill_rect(art->canvas,
                     (Rect){name.x, art->layout.header.height, art->layout.header.width, 1},
                     theme->colors.divider);
}

static Rect inset(Rect area, int amount)
{
    return (Rect){area.x + amount, area.y + amount, area.width - 2 * amount,
                  area.height - 2 * amount};
}

static Rect draw_shell(const ChargeArt *art)
{
    const Theme *theme = art->theme;
    Rect battery = art->layout.battery;
    int cap = larger(1, theme->metrics.padding / 2);
    Rect terminal = {battery.x + battery.width - cap, battery.y + battery.height / 3, cap,
                     battery.height / 3};
    canvas_fill_gradient(art->canvas, terminal, theme->colors.accent_alt,
                         theme->colors.text_secondary);
    battery.width -= cap;
    canvas_fill_gradient(art->canvas, battery, theme->colors.text_secondary,
                         theme->colors.header_background);
    int border = larger(1, theme->metrics.scrollbar_inset);
    Rect glass = inset(battery, border);
    canvas_fill_gradient(art->canvas, glass, theme->colors.backdrop_top,
                         theme->colors.header_background);
    Rect inner = inset(glass, border);
    canvas_fill_rect(art->canvas, inner, theme->colors.background);
    canvas_fill_rect(art->canvas, (Rect){glass.x, glass.y, glass.width, 1}, theme->colors.accent);
    return inner;
}

static void draw_level(const ChargeArt *art, Rect inside)
{
    const Theme *theme = art->theme;
    Rect level = inside;
    level.width =
        art->battery.known ? inside.width * shown_percent(art->battery) / FULL_PERCENT : 0;
    Color ink = flowing(art) || shown_percent(art->battery) == FULL_PERCENT ? theme->colors.success
                                                                            : theme->colors.accent;
    canvas_fill_gradient(art->canvas, level, theme->colors.accent, ink);
    if (flowing(art) && level.width > 0) {
        int width = larger(1, inside.width / CHARGE_SCREEN_PHASES);
        Rect glint = {inside.x + (int)art->phase * level.width / CHARGE_SCREEN_PHASES, inside.y,
                      width, inside.height};
        Canvas clipped = canvas_clipped(art->canvas, level);
        canvas_fill_gradient(&clipped, glint, theme->colors.text, theme->colors.accent_alt);
    }
    int gap = larger(1, theme->metrics.scrollbar_inset);
    for (int cell = 1; cell < BATTERY_CELLS; cell++) {
        canvas_fill_rect(
            art->canvas,
            (Rect){inside.x + cell * inside.width / BATTERY_CELLS, inside.y, gap, inside.height},
            theme->colors.background);
    }
}

static void draw_energy(const ChargeArt *art, Rect inside)
{
    if (!flowing(art)) {
        return;
    }
    const Theme *theme = art->theme;
    int height = inside.height * 3 / 4;
    int width = icon_width(ICON_BOLT, height);
    Rect glyph = {inside.x + (inside.width - width) / 2, inside.y + (inside.height - height) / 2,
                  width, height};
    Rect badge = {glyph.x - theme->metrics.scrollbar_inset, glyph.y,
                  glyph.width + 2 * theme->metrics.scrollbar_inset, glyph.height};
    canvas_fill_rect(art->canvas, badge, theme->colors.header_background);
    int glow = (int)art->phase < CHARGE_SCREEN_PHASES / 2 ? (int)art->phase
                                                          : CHARGE_SCREEN_PHASES - (int)art->phase;
    Color ink =
        color_between(theme->colors.accent_alt, theme->colors.text, glow, CHARGE_SCREEN_PHASES / 2);
    icon_draw(art->canvas, ICON_BOLT, glyph, (IconInk){ink, ink});
}

static void draw_percent(const ChargeArt *art)
{
    char text[TEXT_CAPACITY];
    charge_screen_level_text(art->battery, text, sizeof text);
    if (!art->battery.known) {
        draw_label(art, art->layout.percent, art->theme->fonts.small, text);
        return;
    }
    const Font *font = art->theme->fonts.title;
    Rect area = art->layout.percent;
    int width = text_width(font, text);
    int scale = width * READOUT_SCALE <= area.width ? READOUT_SCALE : 1;
    int x = area.x + (area.width - width * scale) / 2;
    Canvas clipped = canvas_clipped(art->canvas, area);
    const char *cursor = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(font, codepoint);
        if (glyph != NULL) {
            int advance = glyph->advance * scale;
            text_draw_glyph_scaled(&clipped, (TextStyle){font, art->theme->colors.text},
                                   (Rect){x, area.y, advance, area.height}, codepoint, scale);
            x += advance;
        }
    }
}

static void draw_state(const ChargeArt *art)
{
    char text[TEXT_CAPACITY];
    charge_screen_text(art->battery, text, sizeof text);
    const Font *font =
        theme_is_portrait(art->theme) ? art->theme->fonts.title : art->theme->fonts.small;
    draw_label(art, art->layout.status, font, text);
    if (art->battery.usb_power && art->battery.charging) {
        draw_label(art, art->layout.voltage, art->theme->fonts.small, i18n_text(STR_POWER_USB));
    }
}

void charge_art_draw(Canvas *canvas, const Theme *theme, HalBattery battery, uint32_t now_ms)
{
    ChargeArt art = {.canvas = canvas,
                     .theme = theme,
                     .battery = battery,
                     .layout = charge_screen_art_layout(theme),
                     .phase = now_ms / CHARGE_SCREEN_FRAME_MS % CHARGE_SCREEN_PHASES};
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    draw_header(&art);
    Rect inside = draw_shell(&art);
    draw_level(&art, inside);
    draw_energy(&art, inside);
    draw_percent(&art);
    draw_state(&art);
    canvas_fill_rect(canvas,
                     (Rect){art.layout.detail.x, art.layout.detail.y - theme->metrics.padding,
                            art.layout.detail.width, 1},
                     theme->colors.divider);
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){art.layout.detail, i18n_text(STR_CHARGE_MODE_HINT)});
}
