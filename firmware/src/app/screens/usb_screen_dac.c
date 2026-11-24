#include "usb_screen_parts.h"

#include "core/i18n.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "ui/label.h"

enum {
    SEGMENT_PITCH = 6,
    SEGMENT_GAP = 2,
    SEGMENTS_PER_SERVICE = 8,
    GREEN_TENTHS = 7,
    AMBER_TENTHS = 9,
    TENTHS = 10,
    DIM_STEPS = 4,
    DB_STEPS = 256,
    HZ_PER_KHZ = 1000,
    HZ_TENTH = 100,
    BAR_HALF = 2
};

static Color segment_color(const Theme *theme, int index, int count)
{
    if (index * TENTHS >= count * AMBER_TENTHS) {
        return theme->colors.danger;
    }
    return index * TENTHS >= count * GREEN_TENTHS ? theme->colors.accent : theme->colors.success;
}

static int lit_segments(int count, uint8_t level)
{
    return count * level / USB_SCENE_LEVEL_FULL;
}

Rect usb_screen_meter_bar(const Theme *theme, const UsbScreenLayout *layout, int side)
{
    Rect meter = layout->meter;
    int gap = theme->metrics.padding / 2;
    int bar = (meter.height - gap) / 2;
    return (Rect){meter.x, meter.y + side * (bar + gap), meter.width, bar};
}

static int meter_label_width(const Font *font)
{
    int left = text_width(font, i18n_text(STR_USB_DAC_LEFT));
    int right = text_width(font, i18n_text(STR_USB_DAC_RIGHT));
    return left > right ? left : right;
}

static Rect meter_segments(const Theme *theme, Rect row, int label)
{
    int lead = label + theme->metrics.padding / 2;
    row.width -= lead;
    row.x += lead + (row.width % SEGMENT_PITCH + SEGMENT_GAP) / 2;
    return row;
}

static Rect segment_span(Rect bar, int first, int end)
{
    return (Rect){bar.x + first * SEGMENT_PITCH, bar.y, (end - first) * SEGMENT_PITCH - SEGMENT_GAP,
                  bar.height};
}

Rect usb_screen_meter_change(const Theme *theme, const UsbScreenLayout *layout, UsbMeterDelta delta)
{
    Rect row = usb_screen_meter_bar(theme, layout, delta.side);
    Rect bar = meter_segments(theme, row, meter_label_width(theme->fonts.small));
    int count = bar.width / SEGMENT_PITCH;
    int before = lit_segments(count, delta.before);
    int after = lit_segments(count, delta.after);
    if (count <= 0 || row.height <= 0 || before == after) {
        return (Rect){0, 0, 0, 0};
    }
    return before < after ? segment_span(bar, before, after) : segment_span(bar, after, before);
}

static void draw_bar(const UsbPainter *painter, Rect bar, uint8_t level)
{
    Rect clip = painter->canvas->clip;
    int offset = clip.x > bar.x ? clip.x - bar.x : 0;
    int first = (offset + SEGMENT_GAP) / SEGMENT_PITCH;
    int right = clip.x + clip.width - bar.x;
    int end = (right + SEGMENT_PITCH - 1) / SEGMENT_PITCH;
    int count = bar.width / SEGMENT_PITCH;
    end = end < count ? end : count;
    int lit = lit_segments(count, level);
    const Theme *theme = painter->theme;
    for (int index = first; index < end; index++) {
        Color ink = segment_color(theme, index, count);
        if (index >= lit) {
            ink = color_between(theme->colors.background, ink, 1, DIM_STEPS);
        }
        canvas_fill_rect(painter->canvas, segment_span(bar, index, index + 1), ink);
        if ((index - first + 1) % SEGMENTS_PER_SERVICE == 0) {
            usb_screen_service(painter->scene);
        }
    }
}

static void draw_meter(const UsbPainter *painter)
{
    static const StringId SIDES[USB_SCENE_CHANNELS] = {STR_USB_DAC_LEFT, STR_USB_DAC_RIGHT};
    const Theme *theme = painter->theme;
    const Font *font = theme->fonts.small;
    int label = meter_label_width(font);
    for (int side = 0; side < USB_SCENE_CHANNELS; side++) {
        Rect row = usb_screen_meter_bar(theme, &painter->layout, side);
        if (!usb_screen_touches(painter, row)) {
            continue;
        }
        Rect name = {row.x, row.y, label, row.height};
        if (usb_screen_touches(painter, name)) {
            const char *text = i18n_text(SIDES[side]);
            name = label_centered_area(font, name, text);
            usb_screen_text(painter, name, (TextStyle){font, theme->colors.text_secondary, false},
                            text);
        }
        draw_bar(painter, meter_segments(theme, row, label), painter->scene->dac.level[side]);
    }
}

static void signed_text(char *text, int32_t value)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_NUMBER_CAPACITY);
    string_builder_append_char(&builder, value < 0 ? '-' : '+');
    string_builder_append_unsigned(&builder, (uint32_t)(value < 0 ? -value : value));
}

static void rate_text(char *text, const UsbSceneDac *dac)
{
    char rate[USB_SCREEN_NUMBER_CAPACITY];
    char bits[USB_SCREEN_NUMBER_CAPACITY];
    StringBuilder digits = string_builder_make(rate, sizeof rate);
    usb_screen_append_tenths(&digits, dac->rate / HZ_TENTH);
    if (dac->rate % HZ_PER_KHZ == 0) {
        usb_screen_number_text(rate, dac->rate / HZ_PER_KHZ);
    }
    usb_screen_number_text(bits, dac->bits);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {rate, bits};
    i18n_format(&builder, STR_USB_DAC_RATE, arguments, 2);
}

static void host_volume_text(char *text, const UsbSceneDac *dac)
{
    if (dac->host_muted) {
        usb_screen_format_one(text, STR_USB_DAC_MUTED_VALUE, "");
        return;
    }
    char number[USB_SCREEN_NUMBER_CAPACITY];
    int32_t db = dac->host_volume / DB_STEPS;
    signed_text(number, db);
    if (db == 0) {
        usb_screen_number_text(number, 0);
    }
    usb_screen_format_one(text, STR_EQ_GAIN, number);
}

static StringId jack_text(uint8_t jack)
{
    if (jack == HAL_AUDIO_JACK_BALANCED) {
        return STR_JACK_44;
    }
    return jack == HAL_AUDIO_JACK_SINGLE_ENDED ? STR_JACK_35 : STR_ABOUT_NONE;
}

static StringId dac_state_text(const UsbScene *scene)
{
    switch (scene->state) {
    case USB_SCENE_DAC_PLAYING:
        return STR_PLAYING;
    case USB_SCENE_DAC_READY:
        return STR_USB_DAC_IDLE;
    case USB_SCENE_DAC_FAILED:
        return STR_USB_DAC_FAILED;
    default:
        break;
    }
    return STR_USB_DAC_PICK;
}

typedef struct BadgeMemo {
    const Font *title;
    const Font *small;
    const Font *font;
    uint32_t rate;
    uint32_t format_sum;
    int room;
    int inset;
    int width;
    int height;
    uint8_t bits;
} BadgeMemo;

static BadgeMemo badge_memo;
static char badge_text[USB_SCREEN_TEXT_CAPACITY];

static void measure_badge(const UsbPainter *painter, int room, int inset, uint32_t format_sum)
{
    const Theme *theme = painter->theme;
    const UsbScene *scene = painter->scene;
    const Font *font = theme->fonts.title;
    usb_screen_service(scene);
    rate_text(badge_text, &scene->dac);
    usb_screen_service(scene);
    int width = text_width(font, badge_text) + 2 * inset;
    usb_screen_service(scene);
    if (width + 4 > room) {
        font = theme->fonts.small;
        width = text_width(font, badge_text) + 2 * inset;
        usb_screen_service(scene);
    }
    badge_memo = (BadgeMemo){theme->fonts.title,
                             theme->fonts.small,
                             font,
                             scene->dac.rate,
                             format_sum,
                             room,
                             inset,
                             width < room ? width : room,
                             font_line_height(font) + inset,
                             scene->dac.bits};
}

static const BadgeMemo *badge_for(const UsbPainter *painter, int room)
{
    const Theme *theme = painter->theme;
    const UsbSceneDac *dac = &painter->scene->dac;
    const BadgeMemo *badge = &badge_memo;
    int inset = theme->metrics.padding / 2;
    uint32_t sum = usb_screen_text_sum(i18n_text(STR_USB_DAC_RATE));
    if (badge->title != theme->fonts.title || badge->small != theme->fonts.small ||
        badge->rate != dac->rate || badge->bits != dac->bits || badge->format_sum != sum ||
        badge->room != room || badge->inset != inset) {
        measure_badge(painter, room, inset, sum);
    }
    return badge;
}

static void draw_badge(const UsbPainter *painter, Rect frame, const BadgeMemo *badge)
{
    const ThemeColors *colors = &painter->theme->colors;
    canvas_fill_rect(painter->canvas, frame, colors->accent);
    Rect glow = {frame.x + 1, frame.y + 1, frame.width - 2, frame.height - 2};
    canvas_fill_rect(painter->canvas, glow, usb_screen_tint(colors->accent, colors->backdrop_top));
    Rect inside = {frame.x + 2, frame.y + 2, frame.width - 4, frame.height - 4};
    canvas_fill_rect(painter->canvas, inside, colors->backdrop_top);
    int inset = badge->inset;
    Rect words = {frame.x + inset, inside.y, frame.width - 2 * inset, inside.height};
    usb_screen_text(painter, words,
                    (TextStyle){badge->font, usb_screen_tint(colors->accent, colors->text), false},
                    badge_text);
}

static void draw_plug(const UsbPainter *painter, Rect box)
{
    Color ink = painter->theme->colors.text_secondary;
    int middle = box.x + box.width / 2;
    int body = box.height / 2;
    canvas_fill_rect(painter->canvas, (Rect){middle - 3, box.y + box.height - body, 7, body}, ink);
    canvas_fill_rect(painter->canvas, (Rect){middle - 1, box.y + 2, 3, box.height - body - 2}, ink);
    canvas_fill_rect(painter->canvas, (Rect){middle - 1, box.y + box.height - body - 3, 3, 1},
                     painter->theme->colors.backdrop_top);
}

static void draw_output_row(const UsbPainter *painter, Rect row)
{
    const Theme *theme = painter->theme;
    char host[USB_SCREEN_TEXT_CAPACITY];
    host_volume_text(host, &painter->scene->dac);
    int icon = row.height * 3 / 4;
    draw_plug(painter, (Rect){row.x, row.y + (row.height - icon) / 2, icon, icon});
    int gap = theme->metrics.padding / 2;
    Rect rest = {row.x + icon + gap, row.y, row.width - icon - gap, row.height};
    const char *jack = i18n_text(jack_text(painter->scene->dac.jack));
    int jack_width = text_width(theme->fonts.small, jack);
    usb_screen_text(painter, (Rect){rest.x, rest.y, jack_width, rest.height},
                    (TextStyle){theme->fonts.small, theme->colors.text, false}, jack);
    rest.x += jack_width + theme->metrics.padding;
    rest.width -= jack_width + theme->metrics.padding;
    usb_screen_row(painter, rest, i18n_text(STR_USB_DAC_HOST), host);
}

static void draw_breath(const UsbPainter *painter, Rect row)
{
    const Theme *theme = painter->theme;
    const UsbSceneDac *dac = &painter->scene->dac;
    char value[USB_SCREEN_TEXT_CAPACITY];
    char number[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(number, dac->buffer_ms);
    usb_screen_format_one(value, STR_USB_DAC_MS, number);
    const Font *font = theme->fonts.small;
    int name = text_width(font, i18n_text(STR_USB_DAC_BUFFER));
    int note = text_style_width((TextStyle){font, theme->colors.text, true}, value);
    int gap = theme->metrics.padding / 2;
    usb_screen_row(painter, row, i18n_text(STR_USB_DAC_BUFFER), value);
    Rect bar = {row.x + name + gap, row.y + row.height / 2 - BAR_HALF, 0, 2 * BAR_HALF};
    bar.width = row.x + row.width - note - gap - bar.x;
    if (bar.width <= 0) {
        return;
    }
    canvas_fill_rect(
        painter->canvas, bar,
        color_between(theme->colors.backdrop_top, theme->colors.text_tertiary, 1, DIM_STEPS));
    uint32_t full = 2u * (dac->buffer_average_ms > 0 ? dac->buffer_average_ms : 1u);
    uint32_t level = dac->buffer_ms < full ? dac->buffer_ms : full;
    bar.width = (int)(level * (uint32_t)bar.width / full);
    bool live = painter->scene->state == USB_SCENE_DAC_PLAYING;
    canvas_fill_rect(painter->canvas, bar,
                     live ? theme->colors.success : theme->colors.text_tertiary);
}

static void draw_rows(const UsbPainter *painter, Rect lines)
{
    int line = font_line_height(painter->theme->fonts.small);
    int bottom = lines.y + lines.height;
    Rect row = {lines.x, lines.y, lines.width, line};
    if (row.y + line <= bottom && usb_screen_touches(painter, row)) {
        draw_output_row(painter, row);
    }
    row.y += line;
    if (row.y + line <= bottom && usb_screen_touches(painter, row)) {
        char volume[USB_SCREEN_NUMBER_CAPACITY];
        usb_screen_number_text(volume, painter->scene->dac.volume);
        usb_screen_row(painter, row, i18n_text(STR_VOLUME), volume);
    }
    row.y += line;
    if (row.y + line <= bottom && painter->scene->dac.bits != 0 &&
        usb_screen_touches(painter, row)) {
        draw_breath(painter, row);
    }
}

static void draw_menu(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    int top = painter->layout.header.height;
    Rect area = {0, top, theme->metrics.screen_width,
                 painter->layout.detail.y - theme->metrics.padding - top};
    canvas_fill_rect(painter->canvas, area, theme->colors.background);
    usb_screen_service(painter->scene);
    list_view_draw(painter->scene->menu, painter->canvas, area, hal_clock_milliseconds());
    usb_screen_service(painter->scene);
}

static int state_bottom(const UsbPainter *painter)
{
    const char *state = i18n_text(dac_state_text(painter->scene));
    char named[USB_SCREEN_TEXT_CAPACITY];
    if (painter->scene->state == USB_SCENE_DAC_CHOOSE) {
        usb_screen_format_one(named, STR_USB_DAC_PICK, FIRMWARE_SHORT_NAME);
        state = named;
    }
    return usb_screen_draw_state(painter, state);
}

static Rect dac_rows_area(const UsbPainter *painter)
{
    Rect lines = usb_screen_lines_below(painter, state_bottom(painter));
    if (painter->scene->dac.bits == 0) {
        return lines;
    }
    const BadgeMemo *badge = badge_for(painter, lines.width);
    Rect frame = {lines.x, lines.y, badge->width, badge->height};
    if (painter->canvas != NULL && usb_screen_touches(painter, frame)) {
        draw_badge(painter, frame, badge);
    }
    return usb_screen_lines_below(painter, frame.y + frame.height);
}

Rect usb_screen_dac_rows_area(const Theme *theme, const UsbScreenLayout *layout,
                              const UsbScene *scene)
{
    UsbPainter painter = {.theme = theme, .scene = scene, .layout = *layout};
    return dac_rows_area(&painter);
}

void usb_screen_draw_dac(const UsbPainter *painter)
{
    usb_screen_draw_frame(painter, charge_screen_usb_title(painter->status.view));
    usb_screen_service(painter->scene);
    if (painter->scene->menu != NULL) {
        draw_menu(painter);
        return;
    }
    if (painter->details) {
        usb_screen_draw_stats(painter);
        return;
    }
    if (usb_screen_touches(painter, painter->layout.meter)) {
        draw_meter(painter);
    }
    if (!usb_screen_touches(painter, usb_screen_column(&painter->layout))) {
        return;
    }
    usb_screen_service(painter->scene);
    Rect lines = dac_rows_area(painter);
    usb_screen_service(painter->scene);
    draw_rows(painter, lines);
}
