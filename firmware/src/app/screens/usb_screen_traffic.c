#include "usb_screen_parts.h"

#include "core/i18n.h"
#include "core/utf8.h"
#include "ui/label.h"

enum {
    HERO_SCALE = 2,
    ARROW_SHARE = 3,
    ARROW_WIDTH_HALVES = 3,
    BAR_HEIGHT = 6,
    INLINE_WIDTH = 260,
    DIM_STEPS = 5,
    LED_SHARE = 2
};

typedef struct TrafficLayout {
    Rect hero;
    Rect caption;
    Rect drives[USB_SCENE_DRIVES];
    int scale;
    bool inline_bars;
} TrafficLayout;

typedef struct DriveView {
    UsbSceneDrive drive;
    UsbSceneUsage usage;
} DriveView;

typedef struct DriveRow {
    Rect led;
    Rect name;
    Rect arrows;
    Rect bar;
    Rect note;
} DriveRow;

typedef struct HeroRow {
    Rect up;
    Rect down;
    Rect number;
    Rect unit;
} HeroRow;

static int larger(int first, int second)
{
    return first > second ? first : second;
}

static int widest_digit(const Font *font)
{
    char digit[2] = {'0', '\0'};
    int widest = 0;
    for (; digit[0] <= '9'; digit[0]++) {
        widest = larger(widest, text_width(font, digit));
    }
    return widest;
}

static int number_width(const Font *font)
{
    char text[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_rate_text(text, USB_METER_RATE_LIMIT);
    int digit = widest_digit(font);
    int width = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        char one[2] = {*cursor, '\0'};
        width += *cursor >= '0' && *cursor <= '9' ? digit : text_width(font, one);
    }
    return width;
}

static HeroRow hero_row(const Theme *theme, Rect hero, int scale)
{
    int gap = theme->metrics.padding / 2;
    int side = hero.height * 2 / ARROW_SHARE;
    int arrow = (side - gap) / 2;
    int width = (arrow * ARROW_WIDTH_HALVES / 2) | 1;
    int top = hero.y + (hero.height - side) / 2;
    HeroRow row = {.up = {hero.x, top, width, arrow},
                   .down = {hero.x, top + side - arrow, width, arrow}};
    row.number = (Rect){hero.x + row.up.width + gap, hero.y,
                        number_width(theme->fonts.title) * scale, hero.height};
    const Font *small = theme->fonts.small;
    int baseline = hero.y + theme->fonts.title->ascent * scale;
    row.unit = (Rect){row.number.x + row.number.width + gap, baseline - small->ascent,
                      text_width(small, i18n_text(STR_USB_RATE_UNIT)), font_line_height(small)};
    return row;
}

static bool hero_fits(const Theme *theme, Rect column, int scale)
{
    int ascent = theme->fonts.title->ascent;
    Rect hero = {column.x, column.y, column.width, scale * ascent + theme->metrics.padding / 2};
    HeroRow row = hero_row(theme, hero, scale);
    return row.unit.x + row.unit.width <= column.x + column.width;
}

static TrafficLayout traffic_layout(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    Rect column = usb_screen_column(&painter->layout);
    int small = font_line_height(theme->fonts.small);
    int line = font_line_height(theme->fonts.title);
    int gap = theme->metrics.padding / 2;
    TrafficLayout layout = {.inline_bars = column.width >= INLINE_WIDTH};
    int drive = layout.inline_bars ? small : small + BAR_HEIGHT + gap;
    int caption = layout.inline_bars ? small : 2 * small;
    int room = column.height - caption - 2 * drive - 2 * gap;
    int ascent = theme->fonts.title->ascent;
    bool tall = room >= HERO_SCALE * ascent + gap;
    layout.scale = tall && hero_fits(theme, column, HERO_SCALE) ? HERO_SCALE : 1;
    int hero = layout.scale == 1 ? line : layout.scale * ascent + gap;
    layout.hero = (Rect){column.x, column.y, column.width, hero};
    layout.caption = (Rect){column.x, column.y + hero, column.width, caption};
    int bottom = column.y + column.height;
    for (int lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        int top = bottom - (USB_SCENE_DRIVES - lun) * drive - (USB_SCENE_DRIVES - 1 - lun) * gap;
        layout.drives[lun] = (Rect){column.x, top, column.width, drive};
    }
    return layout;
}

int usb_screen_draw_scaled(const UsbPainter *painter, TextStyle style, Rect box, const char *text)
{
    int scale = box.height / style.font->ascent;
    scale = scale < 1 ? 1 : scale;
    int baseline = box.y + style.font->ascent * scale;
    const char *cursor = text;
    for (uint32_t point = utf8_next(&cursor); point != 0; point = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(style.font, point);
        if (glyph != NULL) {
            Rect ink = {box.x + glyph->left * scale, baseline - glyph->top * scale,
                        glyph->width * scale, glyph->height * scale};
            text_draw_glyph_scaled(painter->canvas, style, ink, point, scale);
            box.x += glyph->advance * scale;
        }
    }
    return box.x;
}

static void draw_arrow(const UsbPainter *painter, Rect box, bool down, Color ink)
{
    for (int row = 0; row < box.height; row++) {
        int from_tip = down ? box.height - 1 - row : row;
        int half = (from_tip + 1) * box.width / (2 * box.height);
        canvas_fill_rect(painter->canvas,
                         (Rect){box.x + box.width / 2 - half, box.y + row, 2 * half + 1, 1}, ink);
    }
}

static Color direction_color(const Theme *theme, bool down)
{
    return down ? theme->colors.success : theme->colors.accent;
}

static Color arrow_color(const Theme *theme, bool lit, bool down)
{
    return lit ? direction_color(theme, down) : theme->colors.text_tertiary;
}

static void draw_hero(const UsbPainter *painter, const TrafficLayout *layout)
{
    const Theme *theme = painter->theme;
    const UsbScene *scene = painter->scene;
    HeroRow row = hero_row(theme, layout->hero, layout->scale);
    bool reading = scene->lit[USB_METER_READ];
    bool writing = scene->lit[USB_METER_WRITE];
    draw_arrow(painter, row.up, false, arrow_color(theme, reading, false));
    draw_arrow(painter, row.down, true, arrow_color(theme, writing, true));
    char number[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_rate_text(number, reading || writing ? scene->rate : 0);
    Color ink = reading || writing ? theme->colors.text : theme->colors.text_tertiary;
    TextStyle style = {theme->fonts.title, ink};
    Rect box = row.number;
    box.x += box.width - text_width(style.font, number) * layout->scale;
    usb_screen_draw_scaled(painter, style, box, number);
    usb_screen_text(painter, row.unit,
                    (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                    i18n_text(STR_USB_RATE_UNIT));
}

static void draw_caption(const UsbPainter *painter, Rect area, bool split)
{
    const Theme *theme = painter->theme;
    const UsbScene *scene = painter->scene;
    char read[USB_SCREEN_TEXT_CAPACITY];
    char written[USB_SCREEN_TEXT_CAPACITY];
    usb_screen_size_text(read, scene->meter.sectors[USB_METER_READ]);
    usb_screen_size_text(written, scene->meter.sectors[USB_METER_WRITE]);
    if (split) {
        Rect row = {area.x, area.y, area.width, area.height / 2};
        usb_screen_row(painter, row, i18n_text(STR_USB_STAT_READ), read);
        row.y += row.height;
        usb_screen_row(painter, row, i18n_text(STR_USB_STAT_WRITTEN), written);
        return;
    }
    char text[USB_SCREEN_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    const char *arguments[] = {read, written};
    i18n_format(&builder, STR_USB_TOTALS, arguments, 2);
    usb_screen_text(painter, area, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                    text);
}

static DriveView view_of(const UsbPainter *painter, int lun)
{
    return (DriveView){painter->scene->drives[lun], painter->scene->usage[lun]};
}

static void drive_note(char *text, DriveView view)
{
    if (view.drive.state == USB_SCENE_DRIVE_ABSENT) {
        usb_screen_format_one(text, STR_USB_NO_CARD, "");
    } else if (view.drive.state == USB_SCENE_DRIVE_EJECTED) {
        usb_screen_format_one(text, STR_USB_EJECTED, "");
    } else if (view.usage.free_sectors == USB_SCENE_FREE_UNKNOWN) {
        char size[USB_SCREEN_TEXT_CAPACITY];
        usb_screen_size_text(size, view.drive.sectors);
        usb_screen_format_one(text, STR_USB_TOTAL, size);
    } else {
        char size[USB_SCREEN_TEXT_CAPACITY];
        usb_screen_size_text(size, view.usage.free_sectors);
        usb_screen_format_one(text, STR_USB_FREE, size);
    }
}

static int widest_note(const UsbPainter *painter)
{
    char text[USB_SCREEN_TEXT_CAPACITY];
    int widest = 0;
    for (int lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        drive_note(text, view_of(painter, lun));
        widest = larger(widest, text_width(painter->theme->fonts.small, text));
    }
    return widest;
}

static int widest_name(const Theme *theme)
{
    return larger(text_width(theme->fonts.small, i18n_text(STR_USB_STAT_INTERNAL)),
                  text_width(theme->fonts.small, i18n_text(STR_USB_STAT_CARD)));
}

static DriveRow drive_row(const UsbPainter *painter, const TrafficLayout *layout, int lun)
{
    const Theme *theme = painter->theme;
    Rect area = layout->drives[lun];
    int line = font_line_height(theme->fonts.small);
    int gap = theme->metrics.padding / 2;
    int led = line / LED_SHARE;
    int name = widest_name(theme);
    int note = widest_note(painter);
    int arrow = led + 2;
    DriveRow row = {.led = {area.x, area.y + (line - led) / 2, led, led},
                    .name = {area.x + led + gap, area.y, name, line},
                    .note = {area.x + area.width - note, area.y, note, line}};
    row.arrows =
        (Rect){row.name.x + name + gap, area.y + (line - arrow) / 2, 2 * arrow + gap / 2, arrow};
    int left = row.arrows.x + row.arrows.width + gap;
    row.bar = (Rect){left, area.y + (line - BAR_HEIGHT) / 2, row.note.x - gap - left, BAR_HEIGHT};
    if (!layout->inline_bars) {
        row.bar = (Rect){row.name.x, area.y + line + gap / 2, area.x + area.width - row.name.x,
                         BAR_HEIGHT};
    }
    return row;
}

static Color dim(const Theme *theme, Color color)
{
    return color_between(theme->colors.backdrop_bottom, color, 1, DIM_STEPS);
}

static Color glow_color(const Theme *theme, Color lit, uint8_t glow, bool mounted)
{
    Color rest = dim(theme, mounted ? lit : theme->colors.text_tertiary);
    return mounted ? color_between(rest, lit, glow, USB_METER_GLOW_FULL) : rest;
}

static void draw_lights(const UsbPainter *painter, const DriveRow *row, DriveView view)
{
    const Theme *theme = painter->theme;
    bool mounted = view.drive.state == USB_SCENE_DRIVE_MOUNTED;
    bool used = mounted && view.usage.reads + view.usage.writes != 0;
    canvas_fill_rect(painter->canvas, row->led,
                     used ? theme->colors.success
                          : glow_color(theme, theme->colors.success, 0, mounted));
    Rect down = {row->arrows.x, row->arrows.y, row->arrows.height, row->arrows.height};
    Rect up = {row->arrows.x + row->arrows.width - row->arrows.height, row->arrows.y,
               row->arrows.height, row->arrows.height};
    draw_arrow(painter, down, true,
               glow_color(theme, theme->colors.success, view.usage.write_glow, mounted));
    draw_arrow(painter, up, false,
               glow_color(theme, theme->colors.accent, view.usage.read_glow, mounted));
}

static void draw_tank(const UsbPainter *painter, Rect bar, DriveView view)
{
    const ThemeColors *colors = &painter->theme->colors;
    UsbSceneDrive drive = view.drive;
    uint32_t free = view.usage.free_sectors;
    bool mounted = drive.state == USB_SCENE_DRIVE_MOUNTED;
    Color edge = mounted ? usb_screen_tint(colors->accent, colors->backdrop_bottom)
                         : dim(painter->theme, colors->text_tertiary);
    canvas_fill_rect(painter->canvas, bar, edge);
    Rect inside = {bar.x + 1, bar.y + 1, bar.width - 2, bar.height - 2};
    canvas_fill_rect(painter->canvas, inside, colors->backdrop_bottom);
    if (free == USB_SCENE_FREE_UNKNOWN || free > drive.sectors || drive.sectors == 0 ||
        drive.state == USB_SCENE_DRIVE_ABSENT) {
        return;
    }
    uint64_t used = (uint64_t)(drive.sectors - free) * (uint32_t)inside.width;
    inside.width = (int)(used / drive.sectors);
    canvas_fill_rect(painter->canvas, inside, mounted ? colors->accent : edge);
}

static void draw_drive(const UsbPainter *painter, const TrafficLayout *layout, int lun)
{
    const Theme *theme = painter->theme;
    if (!usb_screen_touches(painter, layout->drives[lun])) {
        return;
    }
    DriveView drive = view_of(painter, lun);
    DriveRow row = drive_row(painter, layout, lun);
    char note[USB_SCREEN_TEXT_CAPACITY];
    drive_note(note, drive);
    draw_lights(painter, &row, drive);
    StringId name = lun == 0 ? STR_USB_STAT_INTERNAL : STR_USB_STAT_CARD;
    usb_screen_text(painter, row.name, (TextStyle){theme->fonts.small, theme->colors.text},
                    i18n_text(name));
    if (row.bar.width > 0) {
        draw_tank(painter, row.bar, drive);
    }
    Rect right = label_centered_area(theme->fonts.small, row.note, note);
    right.x = row.note.x + row.note.width - right.width;
    usb_screen_text(painter, right, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                    note);
}

void usb_screen_draw_traffic(const UsbPainter *painter)
{
    usb_screen_draw_frame(painter, charge_screen_usb_title(painter->status.view));
    if (painter->details) {
        usb_screen_draw_stats(painter);
        return;
    }
    if (usb_screen_touches(painter, painter->layout.art)) {
        usb_screen_draw_graph(painter);
    }
    if (!usb_screen_touches(painter, usb_screen_column(&painter->layout))) {
        return;
    }
    TrafficLayout layout = traffic_layout(painter);
    if (usb_screen_touches(painter, layout.hero)) {
        draw_hero(painter, &layout);
    }
    if (usb_screen_touches(painter, layout.caption)) {
        draw_caption(painter, layout.caption, !layout.inline_bars);
    }
    for (int lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        draw_drive(painter, &layout, lun);
    }
}

Rect usb_screen_drive_lights(const Theme *theme, AppUsbStatus status, int lun)
{
    UsbPainter painter = {.theme = theme,
                          .status = status,
                          .scene = usb_screen_scene(status),
                          .layout = usb_screen_layout(theme, status)};
    TrafficLayout layout = traffic_layout(&painter);
    DriveRow row = drive_row(&painter, &layout, lun);
    int top = row.led.y < row.arrows.y ? row.led.y : row.arrows.y;
    int bottom = row.arrows.y + row.arrows.height;
    return (Rect){row.led.x, top, row.arrows.x + row.arrows.width - row.led.x, bottom - top};
}

Rect usb_screen_activity(const Theme *theme, AppUsbStatus status)
{
    UsbPainter painter = {.theme = theme,
                          .status = status,
                          .scene = usb_screen_scene(status),
                          .layout = usb_screen_layout(theme, status)};
    return traffic_layout(&painter).hero;
}
