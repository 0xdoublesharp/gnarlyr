#include "charge_rate.h"
#include "charge_screen.h"

#include "core/i18n.h"
#include "core/utf8.h"
#include "home_art.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    FULL_PERCENT = 100,
    READOUT_SCALE = 2,
    TEXT_CAPACITY = 64,
    BODY_FIFTHS = 8,
    FIFTHS = 5,
    NUB_DIVISOR = 8,
    STROKE_DIVISOR = 14,
    MIN_STROKE = 2,
    TINT_STEPS = 3,
    GLOW_STEPS = CHARGE_SCREEN_PHASES
};

static ChargeRate charging_rate;

void charge_art_observe(HalBattery battery, uint32_t now_ms)
{
    charge_rate_update(&charging_rate, battery, now_ms, hal_power_usb_host());
}

typedef struct ChargeArt {
    Canvas *canvas;
    const Theme *theme;
    HalBattery battery;
    ChargeScreenLayout layout;
    int glow;
} ChargeArt;

typedef struct BatteryShape {
    Rect body;
    Rect inside;
    Rect fill;
    int stroke;
} BatteryShape;

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

static bool flowing(HalBattery battery)
{
    return battery.usb_power && battery.charging;
}

static bool full(HalBattery battery)
{
    return battery.known && shown_percent(battery) == FULL_PERCENT;
}

static int glow_at(HalBattery battery, uint32_t now_ms)
{
    if (!flowing(battery)) {
        return 0;
    }
    int phase = (int)(now_ms / CHARGE_SCREEN_FRAME_MS % CHARGE_SCREEN_PHASES);
    return phase <= CHARGE_SCREEN_PHASES / 2 ? phase : CHARGE_SCREEN_PHASES - phase;
}

static int battery_width(int height)
{
    return height * BODY_FIFTHS / FIFTHS + larger(1, height / NUB_DIVISOR);
}

static int battery_height(int width)
{
    return width * FIFTHS * NUB_DIVISOR / (BODY_FIFTHS * NUB_DIVISOR + FIFTHS);
}

static Rect battery_at(int center_x, int top, int height)
{
    int width = battery_width(height);
    return (Rect){center_x - width / 2, top, width, height};
}

static ChargeScreenLayout portrait_layout(const Theme *theme, ChargeScreenLayout layout)
{
    int pad = theme->metrics.padding;
    int line = font_line_height(theme->fonts.title);
    int first = theme->metrics.header_height + pad;
    int room = layout.detail.y - 2 * pad - first;
    int text = READOUT_SCALE * line + pad / 2 + line;
    int height = smaller(battery_height(layout.header.width - 2 * pad), room - text - 2 * pad);
    int top = first + (room - height - 2 * pad - text) / 2;
    layout.battery = battery_at(theme->metrics.screen_width / 2, top, height);
    layout.percent =
        (Rect){layout.header.x, top + height + 2 * pad, layout.header.width, READOUT_SCALE * line};
    layout.status = (Rect){layout.header.x, layout.percent.y + layout.percent.height + pad / 2,
                           layout.header.width, line};
    return layout;
}

static ChargeScreenLayout landscape_layout(const Theme *theme, ChargeScreenLayout layout)
{
    int pad = theme->metrics.padding;
    int line = font_line_height(theme->fonts.title);
    int first = theme->metrics.header_height + pad;
    int room = layout.detail.y - 2 * pad - first;
    int side = (layout.header.width - 2 * pad) / 2;
    int height = smaller(battery_height(side - pad), room - pad);
    layout.battery = battery_at(pad + side / 2, first + (room - height) / 2, height);
    int text = READOUT_SCALE * line + line;
    int top = first + (room - text) / 2;
    int right = pad + side + 2 * pad;
    layout.percent = (Rect){right, top, side, READOUT_SCALE * line};
    layout.status = (Rect){right, top + READOUT_SCALE * line, side, line};
    return layout;
}

ChargeScreenLayout charge_screen_art_layout(const Theme *theme)
{
    int pad = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * pad;
    int small = font_line_height(theme->fonts.small);
    const char *hint = i18n_text(STR_CHARGE_MODE_HINT);
    int hint_width = theme_is_portrait(theme) ? width : (width - pad) / 2;
    int detail = label_wrap_count(theme->fonts.small, hint, hint_width) * small;
    detail = theme_is_portrait(theme) ? detail + 2 * small : larger(detail, 2 * small);
    ChargeScreenLayout layout = {
        .header = {pad, 0, width, theme->metrics.header_height},
        .detail = {pad, theme->metrics.screen_height - pad - detail, width, detail}};
    return theme_is_portrait(theme) ? portrait_layout(theme, layout)
                                    : landscape_layout(theme, layout);
}

static Color tint(Color color, Color toward)
{
    return color_between(color, toward, 1, TINT_STEPS);
}

static void draw_label(const ChargeArt *art, Rect area, TextStyle style, const char *text)
{
    area = label_centered_area(style.font, area, text);
    label_draw(art->canvas, style, (Label){area, text});
}

static void draw_header(const ChargeArt *art)
{
    const Theme *theme = art->theme;
    Rect band = {0, 0, theme->metrics.screen_width, theme->metrics.header_height};
    canvas_fill_rect(art->canvas, band, theme->colors.header_background);
    Rect rule = {0, band.height - 1, band.width, 1};
    canvas_fill_rect(art->canvas, rule,
                     theme->header_rule ? theme->colors.accent : theme->colors.divider);
    Rect name = art->layout.header;
    name.height -= 1;
    Rect title = name;
    const char *text = i18n_text(STR_POWER_TITLE);
    if (theme_is_portrait(theme)) {
        name.height = font_line_height(theme->fonts.header);
        title.y += name.height;
        title.height -= name.height;
    } else {
        title.width = text_width(theme->fonts.small, text);
        title.x += name.width - title.width;
    }
    Color ink = theme->colors.header_text;
    label_draw(art->canvas, (TextStyle){theme->fonts.header, ink},
               (Label){name, FIRMWARE_SHORT_NAME});
    label_draw(art->canvas, (TextStyle){theme->fonts.small, tint(theme->colors.accent, ink)},
               (Label){title, text});
}

static int horizon_line(const ChargeArt *art, int rule)
{
    Rect battery = art->layout.battery;
    Rect status = art->layout.status;
    int bottom = larger(battery.y + battery.height, status.y + status.height);
    int horizon = bottom + art->theme->metrics.padding / 2;
    return rule - horizon >= 2 * font_line_height(art->theme->fonts.small) ? horizon : rule;
}

static void draw_stage(const ChargeArt *art)
{
    const ThemeColors *colors = &art->theme->colors;
    int top = art->theme->metrics.header_height;
    int rule = art->layout.detail.y - art->theme->metrics.padding;
    int width = art->theme->metrics.screen_width;
    int horizon = horizon_line(art, rule);
    Color middle = horizon == rule
                       ? colors->backdrop_bottom
                       : color_between(colors->backdrop_top, colors->backdrop_bottom, 1, 2);
    canvas_fill_gradient(art->canvas, (Rect){0, top, width, horizon - top}, colors->backdrop_top,
                         middle);
    Canvas floor = canvas_clipped(art->canvas, (Rect){0, horizon, width, rule - horizon});
    home_art_draw_backdrop(&floor, (Rect){0, 2 * horizon - rule, width, 2 * (rule - horizon)},
                           art->theme);
    canvas_fill_rect(art->canvas, (Rect){0, rule, width, art->theme->metrics.screen_height - rule},
                     colors->background);
    canvas_fill_rect(art->canvas, (Rect){art->layout.detail.x, rule, art->layout.detail.width, 1},
                     colors->divider);
}

static Rect inset(Rect area, int amount)
{
    return (Rect){area.x + amount, area.y + amount, area.width - 2 * amount,
                  area.height - 2 * amount};
}

static void fill_rounded(Canvas *canvas, Rect area, int corner, Color color)
{
    canvas_fill_rect(canvas, (Rect){area.x + corner, area.y, area.width - 2 * corner, area.height},
                     color);
    canvas_fill_rect(canvas, (Rect){area.x, area.y + corner, area.width, area.height - 2 * corner},
                     color);
}

static BatteryShape battery_shape(const ChargeArt *art)
{
    Rect area = art->layout.battery;
    int stroke = larger(MIN_STROKE, area.height / STROKE_DIVISOR);
    int nub = larger(1, area.height / NUB_DIVISOR);
    BatteryShape shape = {.body = {area.x, area.y, area.width - nub, area.height},
                          .stroke = stroke};
    shape.inside = inset(shape.body, stroke);
    shape.fill = inset(shape.inside, stroke);
    shape.fill.width =
        art->battery.known ? shape.fill.width * shown_percent(art->battery) / FULL_PERCENT : 0;
    return shape;
}

static Color fill_color(const ChargeArt *art)
{
    const ThemeColors *colors = &art->theme->colors;
    return art->battery.usb_power ? colors->success : colors->accent;
}

static void draw_body(const ChargeArt *art, const BatteryShape *shape)
{
    const Theme *theme = art->theme;
    Color outline = theme->colors.accent;
    int third = shape->body.height / 3;
    Rect nub = {shape->body.x + shape->body.width, shape->body.y + third,
                art->layout.battery.width - shape->body.width, shape->body.height - 2 * third};
    fill_rounded(art->canvas, nub, nub.width > 2 ? 1 : 0, outline);
    fill_rounded(art->canvas, shape->body, shape->stroke, outline);
    fill_rounded(art->canvas, shape->inside, larger(1, shape->stroke / 2),
                 theme->colors.background);
}

static void draw_charge(const ChargeArt *art, const BatteryShape *shape)
{
    Color ink = color_between(fill_color(art), art->theme->colors.text, art->glow, GLOW_STEPS);
    canvas_fill_gradient(art->canvas, shape->fill, ink, tint(ink, art->theme->colors.background));
}

static void draw_wire_grid(const ChargeArt *art, const BatteryShape *shape)
{
    Canvas inside = canvas_clipped(art->canvas, inset(shape->inside, 1));
    Color ink = color_between(art->theme->colors.accent_alt, art->theme->colors.text, art->glow,
                              GLOW_STEPS);
    int step = larger(6, shape->inside.height / 4);
    int left = shape->inside.x;
    int top = shape->inside.y;
    int right = left + shape->inside.width;
    int bottom = top + shape->inside.height;
    for (int x = left; x <= right; x += step) {
        canvas_fill_rect(&inside, (Rect){x, top, 1, bottom - top}, ink);
    }
    for (int y = top; y < bottom; y += step) {
        canvas_fill_rect(&inside, (Rect){left, y, right - left, 1}, ink);
    }
}

static IconId sign_of(HalBattery battery)
{
    return battery.usb_power && battery.charge_disabled ? ICON_PLUG : ICON_BOLT;
}

static void draw_battery(const ChargeArt *art)
{
    BatteryShape shape = battery_shape(art);
    draw_body(art, &shape);
    draw_charge(art, &shape);
    draw_wire_grid(art, &shape);
}

static void draw_scaled(const ChargeArt *art, Rect area, const char *text)
{
    const Font *font = art->theme->fonts.title;
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

static void draw_percent(const ChargeArt *art)
{
    char text[TEXT_CAPACITY];
    charge_screen_level_text(art->battery, text, sizeof text);
    if (art->battery.known) {
        draw_scaled(art, art->layout.percent, text);
        return;
    }
    const Theme *theme = art->theme;
    draw_label(art, art->layout.percent,
               (TextStyle){theme->fonts.title, theme->colors.text_secondary}, text);
}

static Color state_color(const ChargeArt *art)
{
    const ThemeColors *colors = &art->theme->colors;
    if (flowing(art->battery) || (art->battery.usb_power && full(art->battery))) {
        return tint(colors->success, colors->text);
    }
    return colors->text_secondary;
}

static void draw_state(const ChargeArt *art)
{
    char text[TEXT_CAPACITY];
    charge_screen_text(art->battery, text, sizeof text);
    const Font *font = art->theme->fonts.title;
    if (text_width(font, text) > art->layout.status.width) {
        font = art->theme->fonts.small;
    }
    draw_label(art, art->layout.status, (TextStyle){font, state_color(art)}, text);
    IconId sign = sign_of(art->battery);
    if (flowing(art->battery) || sign == ICON_PLUG) {
        Rect area = art->layout.status;
        int height = font_line_height(art->theme->fonts.small);
        area.x += (area.width + text_width(font, text)) / 2 + art->theme->metrics.padding / 2;
        area.y += (area.height - height) / 2;
        area.width = icon_width(sign, height);
        area.height = height;
        icon_draw(art->canvas, sign, area,
                  (IconInk){art->theme->colors.accent, art->theme->colors.accent});
    }
}

static void draw_speed(const ChargeArt *art, Rect area)
{
    if (charging_rate.rate == 0) {
        return;
    }
    int count = charging_rate.rate >= 45 ? 3 : charging_rate.rate >= 24 ? 2 : 1;
    for (int glyph = 0; glyph < count; glyph++) {
        for (int row = 0; row < 7; row++) {
            int dx = row <= 3 ? row : 6 - row;
            canvas_fill_rect(art->canvas,
                             (Rect){area.x + area.width - 24 + glyph * 8 + dx,
                                    area.y + (area.height - 7) / 2 + row, 2, 1},
                             art->theme->colors.accent);
        }
    }
}

static void draw_trend(const ChargeArt *art)
{
    Rect area = art->layout.detail;
    int pad = art->theme->metrics.padding;
    int line = font_line_height(art->theme->fonts.small);
    bool portrait = theme_is_portrait(art->theme);
    Rect hint = area;
    if (portrait) {
        hint.y += 2 * line;
        hint.height -= 2 * line;
    } else {
        area.width = (area.width - pad) / 2;
        hint.x += area.width + pad;
        hint.width = area.width;
    }
    area.height = line;
    char rate[TEXT_CAPACITY];
    StringBuilder value = string_builder_make(rate, sizeof rate);
    if (flowing(art->battery)) {
        if (charging_rate.rate != 0) {
            i18n_format_counts(&value, STR_CHARGE_RATE, charging_rate.rate, 0);
        } else {
            string_builder_append(&value, i18n_text(STR_CHARGE_MEASURING));
        }
    }
    char summary[TEXT_CAPACITY];
    StringBuilder text = string_builder_make(summary, sizeof summary);
    StringId source = !art->battery.usb_power  ? STR_POWER_BATTERY
                      : charging_rate.computer ? STR_CHARGE_COMPUTER
                                               : STR_CHARGE_CHARGER;
    const char *arguments[] = {i18n_text(source), rate};
    i18n_format(&text, STR_CHARGE_SUMMARY, arguments, 2);
    TextStyle style = {art->theme->fonts.small, art->theme->colors.text_secondary};
    Rect label = area;
    label.width -= charging_rate.rate != 0 ? 26 : 0;
    label_draw(art->canvas, style, (Label){label, summary});
    draw_speed(art, area);
    area.y += line;
    if (charging_rate.minutes != 0 && flowing(art->battery)) {
        text = string_builder_make(summary, sizeof summary);
        StringId pattern =
            charging_rate.minutes >= 60 ? STR_CHARGE_FULL_IN : STR_CHARGE_FULL_MINUTES;
        i18n_format_counts(&text, pattern,
                           charging_rate.minutes >= 60 ? charging_rate.minutes / 60
                                                       : charging_rate.minutes,
                           charging_rate.minutes % 60);
        label_draw(art->canvas, style, (Label){area, summary});
    }
    label_draw_wrapped(art->canvas, style, (Label){hint, i18n_text(STR_CHARGE_MODE_HINT)});
}

void charge_art_draw(Canvas *canvas, const Theme *theme, HalBattery battery, uint32_t now_ms)
{
    charge_art_observe(battery, now_ms);
    Theme charging = *theme;
    charging.metrics.header_height = font_line_height(theme->fonts.header) + 2;
    if (theme_is_portrait(theme)) {
        charging.metrics.header_height += font_line_height(theme->fonts.small);
    }
    theme = &charging;
    ChargeArt art = {.canvas = canvas,
                     .theme = theme,
                     .battery = battery,
                     .layout = charge_screen_art_layout(theme),
                     .glow = glow_at(battery, now_ms)};
    draw_stage(&art);
    draw_header(&art);
    draw_battery(&art);
    draw_percent(&art);
    draw_state(&art);
    draw_trend(&art);
}
