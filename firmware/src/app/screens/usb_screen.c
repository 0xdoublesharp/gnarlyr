#include "usb_screen_parts.h"

#include "core/i18n.h"
#include "home_art.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/wire_figure.h"

enum {
    FULL_PERCENT = 100,
    TINT_STEPS = 3,
    LAMP_DIVISOR = 3,
    ART_COLUMN_FIFTHS = 2,
    FIFTHS = 5,
    STATE_LINES = 2,
    METER_BAR_LINES = 2,
    ART_TALL_PARTS = 5,
    ART_WIDE_PARTS = 8,
    FLOOR_QUARTERS = 3,
    SPAN_QUARTERS = 5,
    FIGURE_WIDE_TENTHS = 12,
    FIGURE_TALL_TENTHS = 8,
    QUARTERS = 4,
    BATTERY_WIDTH = 24,
    BATTERY_HEIGHT = 12,
    BATTERY_NUB = 2,
    TAG_LUMA_GAP = 64,
    MSC_BYTES_PER_SECTOR = 512,
    BYTES_PER_KIB = 1024,
    BYTES_PER_TENTH_MB = 100000,
    MB_PER_GB = 1000,
    TENTHS = 10,
    WHOLE_FROM = 1000,
    KB_PER_TENTH_MB = 100,
    MEMO_PRIME = 31,
    SECONDS_PER_MINUTE = 60,
    SECONDS_PER_HOUR = 3600
};

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static int larger(int first, int second)
{
    return first > second ? first : second;
}

const UsbScene *usb_screen_scene(AppUsbStatus status)
{
    static const UsbScene PLAIN = {.state = USB_SCENE_CONNECTED};
    static const UsbScene STARTING = {.state = USB_SCENE_STARTING};
    if (status.scene != NULL) {
        return status.scene;
    }
    return status.view == APP_USB_CONNECTING ? &STARTING : &PLAIN;
}

void usb_screen_service(const UsbScene *scene)
{
    if (scene->service != NULL) {
        scene->service();
    }
}

typedef struct DetailMemo {
    const Theme *theme;
    const char *text;
    uint32_t sum;
    int width;
    int lines;
} DetailMemo;

static DetailMemo detail_memo;

static uint32_t text_sum(const char *text)
{
    uint32_t sum = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        sum = sum * MEMO_PRIME + (uint8_t)*cursor;
    }
    return sum;
}

static int detail_lines(const Theme *theme, const char *detail, int width)
{
    uint32_t sum = text_sum(detail);
    DetailMemo *memo = &detail_memo;
    if (memo->theme != theme || memo->text != detail || memo->sum != sum || memo->width != width) {
        *memo = (DetailMemo){theme, detail, sum, width,
                             label_wrap_count(theme->fonts.small, detail, width)};
    }
    return memo->lines;
}

static Rect detail_area(const Theme *theme, const char *detail)
{
    int pad = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * pad;
    int lines = detail == NULL ? 0 : detail_lines(theme, detail, width);
    int height = lines * font_line_height(theme->fonts.small);
    return (Rect){pad, theme->metrics.screen_height - pad - height, width, height};
}

static Rect meter_in(const Theme *theme, Rect region)
{
    int bar = smaller(METER_BAR_LINES * font_line_height(theme->fonts.small),
                      (region.height - theme->metrics.padding) / 2);
    int height = 2 * bar + theme->metrics.padding / 2;
    return (Rect){region.x, region.y + (region.height - height) / 2, region.width, height};
}

static Rect figure_in(Rect art)
{
    int span = smaller(art.width, art.height * SPAN_QUARTERS / QUARTERS);
    int width = smaller(art.width, span * FIGURE_WIDE_TENTHS / TENTHS);
    int height = smaller(art.height, span * FIGURE_TALL_TENTHS / TENTHS);
    return (Rect){art.x + (art.width - width) / 2, art.y + (art.height - height) / 2, width,
                  height};
}

static UsbScreenLayout portrait_layout(const Theme *theme, UsbScreenLayout layout)
{
    int pad = theme->metrics.padding;
    int top = layout.header.y + layout.header.height + pad;
    int height = layout.header.width * ART_TALL_PARTS / ART_WIDE_PARTS;
    int bottom = layout.detail.y - pad;
    Rect art = {layout.header.x, top, layout.header.width, height};
    layout.art = art;
    layout.figure = figure_in(art);
    layout.meter = meter_in(theme, art);
    int below = top + height + pad;
    int state = STATE_LINES * font_line_height(theme->fonts.title);
    layout.state = (Rect){layout.header.x, below, layout.header.width, state};
    layout.lines = (Rect){layout.header.x, below + state + pad / 2, layout.header.width,
                          bottom - below - state - pad / 2};
    return layout;
}

static UsbScreenLayout landscape_layout(const Theme *theme, UsbScreenLayout layout)
{
    int pad = theme->metrics.padding;
    int top = layout.header.y + layout.header.height + pad;
    int bottom = layout.detail.y - pad;
    int column = layout.header.width * ART_COLUMN_FIFTHS / FIFTHS;
    layout.art = (Rect){layout.header.x, top, column, bottom - top};
    layout.figure = figure_in(layout.art);
    layout.meter = meter_in(theme, layout.art);
    int right = layout.header.x + column + pad;
    int width = layout.header.x + layout.header.width - right;
    int state = STATE_LINES * font_line_height(theme->fonts.title);
    layout.state = (Rect){right, top, width, state};
    layout.lines = (Rect){right, top + state + pad / 2, width, bottom - top - state - pad / 2};
    return layout;
}

UsbScreenLayout usb_screen_layout(const Theme *theme, AppUsbStatus status)
{
    int pad = theme->metrics.padding;
    UsbScreenLayout layout = {
        .header = {pad, 0, theme->metrics.screen_width - 2 * pad, theme->metrics.header_height},
        .detail = detail_area(theme, status.detail)};
    return theme_is_portrait(theme) ? portrait_layout(theme, layout)
                                    : landscape_layout(theme, layout);
}

Color usb_screen_tint(Color color, Color toward)
{
    return color_between(color, toward, 1, TINT_STEPS);
}

void usb_screen_text(const UsbPainter *painter, Rect area, TextStyle style, const char *text)
{
    usb_screen_service(painter->scene);
    label_draw(painter->canvas, style, (Label){area, text});
    usb_screen_service(painter->scene);
}

static void draw_battery_icon(const UsbPainter *painter, Rect body)
{
    const ThemeColors *colors = &painter->theme->colors;
    HalBattery battery = painter->battery;
    Color ink = colors->header_text;
    Rect nub = {body.x + body.width, body.y + body.height / 3, BATTERY_NUB, body.height / 3};
    canvas_fill_rect(painter->canvas, body, ink);
    canvas_fill_rect(painter->canvas, nub, ink);
    Rect inside = {body.x + 1, body.y + 1, body.width - 2, body.height - 2};
    canvas_fill_rect(painter->canvas, inside, colors->header_background);
    if (!battery.known) {
        return;
    }
    Rect fill = {inside.x + 1, inside.y + 1, inside.width - 2, inside.height - 2};
    fill.width = larger(1, fill.width * smaller(battery.percent, FULL_PERCENT) / FULL_PERCENT);
    canvas_fill_rect(painter->canvas, fill, battery.usb_power ? colors->success : ink);
}

static int draw_battery(const UsbPainter *painter, Rect row)
{
    const Theme *theme = painter->theme;
    char text[USB_SCREEN_TEXT_CAPACITY];
    charge_screen_level_text(painter->battery, text, sizeof text);
    Rect body = {row.x + row.width - BATTERY_WIDTH - BATTERY_NUB,
                 row.y + (row.height - BATTERY_HEIGHT) / 2, BATTERY_WIDTH, BATTERY_HEIGHT};
    draw_battery_icon(painter, body);
    int width = text_width(theme->fonts.small, text);
    int left = body.x - theme->metrics.padding / 2 - width;
    usb_screen_text(painter, (Rect){left, row.y, width, row.height},
                    (TextStyle){theme->fonts.small, theme->colors.header_text, false}, text);
    return left - row.x;
}

typedef struct SpeedTag {
    bool shown;
    bool bolt;
    Color ink;
    int height;
    int width;
} SpeedTag;

static bool linked(const UsbPainter *painter)
{
    const UsbScene *scene = painter->scene;
    if (painter->status.scene == NULL) {
        return false;
    }
    if (painter->status.view == APP_USB_DAC) {
        return scene->state == USB_SCENE_DAC_READY || scene->state == USB_SCENE_DAC_PLAYING;
    }
    return painter->status.view == APP_USB_STORAGE && usb_screen_shows_traffic(scene);
}

static int color_luma(Color color)
{
    int red = (color >> 11) * 255 / 31;
    int green = (color >> 5 & 63) * 255 / 63;
    int blue = (color & 31) * 255 / 31;
    return (54 * red + 183 * green + 19 * blue) >> 8;
}

static Color warning_ink(const ThemeColors *colors)
{
    int gap = color_luma(colors->danger) - color_luma(colors->header_background);
    gap = gap < 0 ? -gap : gap;
    return gap >= TAG_LUMA_GAP ? colors->danger
                               : color_between(colors->danger, colors->header_text, 1, 2);
}

static SpeedTag speed_tag(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    int height = theme->fonts.small->ascent;
    int width = icon_width(ICON_USB, height) + icon_width(ICON_BOLT, height);
    if (!linked(painter)) {
        return (SpeedTag){.shown = false};
    }
    const UsbScene *scene = painter->scene;
    bool warn = !scene->high_speed && scene->fell_back;
    Color ink = warn ? warning_ink(&theme->colors) : theme->colors.header_text;
    return (SpeedTag){true, scene->high_speed, ink, height, width + theme->metrics.padding / 2};
}

static void draw_tag(const UsbPainter *painter, Rect row, int after, SpeedTag tag)
{
    if (!tag.shown) {
        return;
    }
    int left = after + painter->theme->metrics.padding / 2;
    int top = row.y + (row.height - tag.height) / 2;
    Rect usb = {left, top, icon_width(ICON_USB, tag.height), tag.height};
    IconInk ink = {tag.ink, tag.ink};
    icon_draw(painter->canvas, ICON_USB, usb, ink);
    if (tag.bolt) {
        Rect bolt = {usb.x + usb.width, top, icon_width(ICON_BOLT, tag.height), tag.height};
        icon_draw(painter->canvas, ICON_BOLT, bolt, ink);
    }
}

static Rect header_band(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    Rect band = {0, 0, theme->metrics.screen_width, theme->metrics.header_height};
    canvas_fill_rect(painter->canvas, band, theme->colors.header_background);
    canvas_fill_rect(painter->canvas, (Rect){0, band.height - 1, band.width, 1},
                     theme->header_rule ? theme->colors.accent : theme->colors.divider);
    Rect row = painter->layout.header;
    row.height -= 1;
    return row;
}

static const char *header_title(const UsbPainter *painter, const char *title)
{
    bool storage = painter->status.view == APP_USB_STORAGE;
    return storage && linked(painter) ? i18n_text(STR_USB_STATE_CONNECTED) : title;
}

static void draw_header(const UsbPainter *painter, const char *mode_title)
{
    const Theme *theme = painter->theme;
    const char *title = header_title(painter, mode_title);
    Rect row = header_band(painter);
    Rect second = row;
    if (theme_is_portrait(theme)) {
        row.height = font_line_height(theme->fonts.header);
        second.y += row.height;
        second.height -= row.height;
    }
    int room = draw_battery(painter, row);
    Color ink = theme->colors.header_text;
    TextStyle name = {theme->fonts.header, ink, false};
    TextStyle mode = {theme->fonts.small, usb_screen_tint(theme->colors.accent, ink), false};
    int title_width = text_width(theme->fonts.small, title);
    SpeedTag tag = speed_tag(painter);
    if (theme_is_portrait(theme)) {
        usb_screen_text(painter, (Rect){row.x, row.y, room, row.height}, name, FIRMWARE_SHORT_NAME);
        Rect status = {second.x, second.y, second.width - tag.width, second.height};
        usb_screen_text(painter, status, mode, title);
        draw_tag(painter, second, second.x + second.width - tag.width, tag);
        return;
    }
    int gap = theme->metrics.padding;
    int title_x = row.x + room - gap - title_width - tag.width;
    usb_screen_text(painter, (Rect){row.x, row.y, title_x - gap - row.x, row.height}, name,
                    FIRMWARE_SHORT_NAME);
    usb_screen_text(painter, (Rect){title_x, row.y, title_width, row.height}, mode, title);
    draw_tag(painter, (Rect){row.x, row.y, row.x + room - row.x, row.height}, title_x + title_width,
             tag);
}

static void draw_floor(const UsbPainter *painter, int rule)
{
    const Theme *theme = painter->theme;
    Rect art = painter->layout.art;
    int horizon = art.y + art.height * FLOOR_QUARTERS / QUARTERS;
    int left = theme_is_portrait(theme) ? 0 : art.x;
    int width = theme_is_portrait(theme) ? theme->metrics.screen_width : art.width;
    int floor = theme_is_portrait(theme) ? art.y + art.height : rule;
    if (floor - horizon < 2) {
        return;
    }
    Canvas clipped = canvas_clipped(painter->canvas, (Rect){left, horizon, width, floor - horizon});
    home_art_draw_backdrop(&clipped,
                           (Rect){left, 2 * horizon - floor, width, 2 * (floor - horizon)}, theme);
}

static void draw_stage(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    const ThemeColors *colors = &theme->colors;
    int top = theme->metrics.header_height;
    int rule = painter->layout.detail.y - theme->metrics.padding;
    int width = theme->metrics.screen_width;
    canvas_fill_gradient(painter->canvas, (Rect){0, top, width, rule - top}, colors->backdrop_top,
                         colors->backdrop_bottom);
    bool terrain = painter->status.view == APP_USB_STORAGE && painter->status.scene != NULL &&
                   usb_screen_shows_traffic(painter->scene);
    if (painter->status.view != APP_USB_DAC && !painter->details && !terrain) {
        draw_floor(painter, rule);
    }
    canvas_fill_rect(painter->canvas, (Rect){0, rule, width, theme->metrics.screen_height - rule},
                     colors->background);
    canvas_fill_rect(painter->canvas,
                     (Rect){painter->layout.detail.x, rule, painter->layout.detail.width, 1},
                     colors->divider);
}

static bool warning(const UsbScene *scene)
{
    return scene->state == USB_SCENE_UNPLUGGED || scene->state == USB_SCENE_STOPPED ||
           scene->state == USB_SCENE_DAC_FAILED;
}

Color usb_screen_lamp_color(const Theme *theme, const UsbScene *scene)
{
    const ThemeColors *colors = &theme->colors;
    switch (scene->state) {
    case USB_SCENE_WRITING:
    case USB_SCENE_DAC_PLAYING:
        return colors->success;
    case USB_SCENE_READING:
    case USB_SCENE_WAITING:
    case USB_SCENE_LEAVING:
    case USB_SCENE_STARTING:
        return colors->accent;
    default:
        break;
    }
    return warning(scene) ? colors->danger : colors->text_tertiary;
}

static void draw_cassette(const UsbPainter *painter)
{
    Theme ink = *painter->theme;
    if (warning(painter->scene)) {
        ink.colors.accent = ink.colors.danger;
    }
    WireFigureFrame frame = {WIRE_SHAPE_CASSETTE, painter->scene->turn, WIRE_FIGURE_ENDLESS};
    Canvas figure = canvas_clipped(painter->canvas, painter->layout.figure);
    wire_figure_draw(&figure, &ink, painter->layout.art, frame);
}

static StringId state_text(const UsbScene *scene)
{
    static const StringId TEXTS[] = {
        [USB_SCENE_STARTING] = STR_POWER_CONNECTING,
        [USB_SCENE_CONNECTED] = STR_USB_STATE_CONNECTED,
        [USB_SCENE_READING] = STR_USB_STATE_READING,
        [USB_SCENE_WRITING] = STR_USB_STATE_WRITING,
        [USB_SCENE_WAITING] = STR_USB_STATE_WAITING,
        [USB_SCENE_LEAVING] = STR_USB_STATE_LEAVING,
        [USB_SCENE_UNPLUGGED] = STR_USB_UNPLUGGED,
        [USB_SCENE_STOPPED] = STR_USB_STOPPED,
    };
    return scene->state < sizeof TEXTS / sizeof TEXTS[0] ? TEXTS[scene->state]
                                                         : STR_USB_STATE_CONNECTED;
}

int usb_screen_draw_state(const UsbPainter *painter, const char *text)
{
    const Theme *theme = painter->theme;
    const Font *font = theme->fonts.title;
    Rect area = painter->layout.state;
    int line = font_line_height(font);
    int lamp = larger(4, line / LAMP_DIVISOR);
    int indent = lamp + theme->metrics.padding / 2;
    int width = area.width - indent;
    int lines = smaller(STATE_LINES, label_wrap_count(font, text, width));
    Rect words = {area.x + indent, area.y, width, lines * line};
    Rect dot = {area.x, area.y + (line - lamp) / 2, lamp, lamp};
    canvas_fill_rect(painter->canvas, dot, usb_screen_lamp_color(theme, painter->scene));
    Color ink = warning(painter->scene) ? usb_screen_tint(theme->colors.danger, theme->colors.text)
                                        : theme->colors.text;
    label_draw_paragraph(painter->canvas, (TextStyle){font, ink, false}, (Label){words, text});
    return words.y + words.height;
}

void usb_screen_append_tenths(StringBuilder *builder, uint32_t tenths)
{
    string_builder_append_unsigned(builder, tenths / TENTHS);
    if (tenths < WHOLE_FROM) {
        string_builder_append_char(builder, '.');
        string_builder_append_unsigned(builder, tenths % TENTHS);
    }
}

static void amount_text(char *text, uint32_t tenths_mb)
{
    char number[USB_SCREEN_NUMBER_CAPACITY];
    StringBuilder digits = string_builder_make(number, sizeof number);
    bool gigabytes = tenths_mb >= TENTHS * MB_PER_GB;
    usb_screen_append_tenths(&digits, gigabytes ? tenths_mb / MB_PER_GB : tenths_mb);
    usb_screen_format_one(text, gigabytes ? STR_USB_GB : STR_USB_MB, number);
}

void usb_screen_size_text(char *text, uint32_t sectors)
{
    uint32_t tenths = (uint32_t)((uint64_t)sectors * MSC_BYTES_PER_SECTOR / BYTES_PER_TENTH_MB);
    amount_text(text, tenths == 0 && sectors != 0 ? 1 : tenths);
}

void usb_screen_number_text(char *text, uint32_t value)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_NUMBER_CAPACITY);
    string_builder_append_unsigned(&builder, value);
}

void usb_screen_format_one(char *text, StringId id, const char *argument)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    i18n_format_text(&builder, id, argument);
}

void usb_screen_rate_text(char *text, uint32_t kilobytes_per_second)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_NUMBER_CAPACITY);
    uint32_t tenths = (kilobytes_per_second + KB_PER_TENTH_MB / 2) / KB_PER_TENTH_MB;
    string_builder_append_unsigned(&builder, tenths / TENTHS);
    string_builder_append_char(&builder, '.');
    string_builder_append_unsigned(&builder, tenths % TENTHS);
}

static void append_two_digits(StringBuilder *builder, uint32_t value)
{
    string_builder_append_char(builder, (char)('0' + value / TENTHS));
    string_builder_append_char(builder, (char)('0' + value % TENTHS));
}

void usb_screen_time_text(char *text, uint32_t seconds)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    uint32_t hours = seconds / SECONDS_PER_HOUR;
    uint32_t minutes = seconds / SECONDS_PER_MINUTE % SECONDS_PER_MINUTE;
    if (hours > 0) {
        string_builder_append_unsigned(&builder, hours);
        string_builder_append_char(&builder, ':');
        append_two_digits(&builder, minutes);
    } else {
        string_builder_append_unsigned(&builder, minutes);
    }
    string_builder_append_char(&builder, ':');
    append_two_digits(&builder, seconds % SECONDS_PER_MINUTE);
}

void usb_screen_row(const UsbPainter *painter, Rect row, const char *name, const char *value)
{
    const Theme *theme = painter->theme;
    const Font *font = theme->fonts.small;
    TextStyle figures = {font, theme->colors.text, true};
    int value_width = smaller(text_style_width(figures, value), row.width);
    Rect right = {row.x + row.width - value_width, row.y, value_width, row.height};
    Rect left = {row.x, row.y, row.width - value_width - theme->metrics.padding / 2, row.height};
    usb_screen_text(painter, left, (TextStyle){font, theme->colors.text_secondary, false}, name);
    usb_screen_text(painter, right, figures, value);
}

static void draw_drive(const UsbPainter *painter, Rect row, StringId name, UsbSceneDrive drive)
{
    char size[USB_SCREEN_TEXT_CAPACITY];
    if (drive.state == USB_SCENE_DRIVE_EJECTED) {
        usb_screen_row(painter, row, i18n_text(name), i18n_text(STR_USB_EJECTED));
        return;
    }
    usb_screen_size_text(size, drive.sectors);
    usb_screen_row(painter, row, i18n_text(name), size);
}

static void next_row(Rect *row)
{
    row->y += row->height;
}

static void draw_storage_lines(const UsbPainter *painter, Rect area)
{
    const UsbScene *scene = painter->scene;
    int bottom = area.y + area.height;
    Rect row = {area.x, area.y, area.width, font_line_height(painter->theme->fonts.small)};
    char text[USB_SCREEN_TEXT_CAPACITY];
    if (scene->written_kib > 0 && !warning(scene) && row.y + row.height <= bottom) {
        amount_text(text,
                    (uint32_t)((uint64_t)scene->written_kib * BYTES_PER_KIB / BYTES_PER_TENTH_MB));
        usb_screen_row(painter, row, i18n_text(STR_USB_COPIED), text);
        next_row(&row);
    }
    static const StringId NAMES[USB_SCENE_DRIVES] = {STR_POWER_INTERNAL, STR_USB_CARD};
    for (int lun = 0; lun < USB_SCENE_DRIVES; lun++) {
        UsbSceneDrive drive = scene->drives[lun];
        if (drive.state != USB_SCENE_DRIVE_ABSENT && row.y + row.height <= bottom) {
            draw_drive(painter, row, NAMES[lun], drive);
            next_row(&row);
        }
    }
    if (!warning(scene) && row.y + row.height <= bottom) {
        usb_screen_row(painter, row, i18n_text(STR_USB),
                       i18n_text(scene->high_speed ? STR_USB_HIGH_SPEED : STR_USB_FULL_SPEED));
    }
}

bool usb_screen_touches(const UsbPainter *painter, Rect area)
{
    Rect clip = painter->canvas->clip;
    return area.width > 0 && area.height > 0 && clip.x < area.x + area.width &&
           area.x < clip.x + clip.width && clip.y < area.y + area.height &&
           area.y < clip.y + clip.height;
}

static void show_row(AppUsbStatus status, Rect row, int piece)
{
    for (int left = row.x; left < row.x + row.width; left += piece) {
        int width = row.x + row.width - left;
        app_show_usb_area(status, (Rect){left, row.y, width < piece ? width : piece, row.height});
    }
}

void usb_screen_show_in_bands(AppUsbStatus status, Rect area)
{
    bool streaming = status.view == APP_USB_DAC && usb_screen_scene(status)->high_speed;
    int band = streaming ? 1 : USB_SCREEN_BAND_HEIGHT;
    int piece = streaming ? USB_SCREEN_BAND_WIDTH : area.width;
    for (int top = area.y; top < area.y + area.height; top += band) {
        int height = area.y + area.height - top;
        height = height < band ? height : band;
        show_row(status, (Rect){area.x, top, area.width, height}, piece > 0 ? piece : 1);
    }
}

Rect usb_screen_graph(const Theme *theme, const UsbScreenLayout *layout)
{
    int inset = theme->metrics.padding / 2;
    Rect art = layout->art;
    return (Rect){art.x, art.y + inset, art.width, art.height - 2 * inset};
}

Rect usb_screen_content(const Theme *theme, const UsbScreenLayout *layout)
{
    int top = layout->header.y + layout->header.height + theme->metrics.padding;
    int bottom = layout->detail.y - theme->metrics.padding - theme->metrics.padding / 2;
    return (Rect){layout->header.x, top, layout->header.width, bottom - top};
}

int usb_screen_pages(const Theme *theme, AppUsbStatus status)
{
    const UsbScene *scene = usb_screen_scene(status);
    bool storage = status.view == APP_USB_STORAGE && usb_screen_shows_traffic(scene);
    bool dac = status.view == APP_USB_DAC && scene->menu == NULL;
    if (status.scene == NULL || !(storage || dac)) {
        return 1;
    }
    UsbPainter painter = {.theme = theme,
                          .status = status,
                          .scene = scene,
                          .layout = usb_screen_layout(theme, status)};
    return 1 + usb_screen_stats_pages(&painter);
}

Rect usb_screen_column(const UsbScreenLayout *layout)
{
    return (Rect){layout->lines.x, layout->state.y, layout->lines.width,
                  layout->lines.y + layout->lines.height - layout->state.y};
}

static const char *detail_text(const UsbPainter *painter)
{
    bool nudge = painter->status.view == APP_USB_STORAGE && painter->scene->nudge;
    return nudge ? i18n_text(STR_USB_HOLD_BACK_HINT) : painter->status.detail;
}

void usb_screen_draw_frame(const UsbPainter *painter, const char *title)
{
    draw_stage(painter);
    usb_screen_service(painter->scene);
    const Theme *theme = painter->theme;
    if (usb_screen_touches(painter, painter->layout.header)) {
        draw_header(painter, title);
    }
    if (usb_screen_touches(painter, painter->layout.detail)) {
        label_draw_wrapped_serviced(
            painter->canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
            (Label){painter->layout.detail, detail_text(painter)}, painter->scene->service);
    }
}

bool usb_screen_shows_traffic(const UsbScene *scene)
{
    return scene->state == USB_SCENE_CONNECTED || scene->state == USB_SCENE_READING ||
           scene->state == USB_SCENE_WRITING;
}

static void draw_storage(const UsbPainter *painter)
{
    if (painter->status.view == APP_USB_STORAGE && usb_screen_shows_traffic(painter->scene) &&
        painter->status.scene != NULL) {
        usb_screen_draw_traffic(painter);
        return;
    }
    usb_screen_draw_frame(painter, charge_screen_usb_title(painter->status.view));
    if (usb_screen_touches(painter, painter->layout.figure)) {
        draw_cassette(painter);
    }
    if (usb_screen_touches(painter, usb_screen_column(&painter->layout))) {
        int below = usb_screen_draw_state(painter, i18n_text(state_text(painter->scene)));
        draw_storage_lines(painter, usb_screen_lines_below(painter, below));
    }
}

Rect usb_screen_lines_below(const UsbPainter *painter, int below)
{
    Rect lines = painter->layout.lines;
    int top = below + painter->theme->metrics.padding / 2;
    return (Rect){lines.x, top, lines.width, lines.y + lines.height - top};
}

void usb_screen_draw_power(const UsbPainter *painter)
{
    char state[USB_SCREEN_TEXT_CAPACITY];
    char level[USB_SCREEN_TEXT_CAPACITY];
    usb_screen_draw_frame(painter, charge_screen_usb_title(painter->status.view));
    draw_cassette(painter);
    charge_screen_text(painter->battery, state, sizeof state);
    int below = usb_screen_draw_state(painter, state);
    Rect lines = usb_screen_lines_below(painter, below);
    lines.height = font_line_height(painter->theme->fonts.small);
    charge_screen_level_text(painter->battery, level, sizeof level);
    usb_screen_row(painter, lines, i18n_text(STR_POWER_TITLE), level);
}

void usb_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery, AppUsbStatus status)
{
    usb_screen_service(usb_screen_scene(status));
    UsbPainter painter = {.canvas = canvas,
                          .theme = theme,
                          .battery = battery,
                          .status = status,
                          .scene = usb_screen_scene(status),
                          .layout = usb_screen_layout(theme, status)};
    usb_screen_service(painter.scene);
    painter.details = painter.scene->page != USB_SCENE_OVERVIEW &&
                      painter.scene->page < usb_screen_pages(theme, status);
    usb_screen_service(painter.scene);
    if (status.view == APP_USB_DAC) {
        usb_screen_draw_dac(&painter);
    } else if (status.view == APP_USB_POWER) {
        usb_screen_draw_power(&painter);
    } else {
        draw_storage(&painter);
    }
}
