#include "usb_screen_parts.h"

#include "core/i18n.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "ui/label.h"

enum {
    SEGMENT_PITCH = 6,
    SEGMENT_GAP = 2,
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

static void draw_bar(const UsbPainter *painter, Rect bar, uint8_t level)
{
    const Theme *theme = painter->theme;
    int count = bar.width / SEGMENT_PITCH;
    int lit = count * level / USB_SCENE_LEVEL_FULL;
    int left = bar.x + (bar.width - count * SEGMENT_PITCH + SEGMENT_GAP) / 2;
    for (int index = 0; index < count; index++) {
        Color ink = segment_color(theme, index, count);
        if (index >= lit) {
            ink = color_between(theme->colors.background, ink, 1, DIM_STEPS);
        }
        Rect segment = {left + index * SEGMENT_PITCH, bar.y, SEGMENT_PITCH - SEGMENT_GAP,
                        bar.height};
        canvas_fill_rect(painter->canvas, segment, ink);
    }
}

Rect usb_screen_meter_bar(const Theme *theme, const UsbScreenLayout *layout, int side)
{
    Rect meter = layout->meter;
    int gap = theme->metrics.padding / 2;
    int bar = (meter.height - gap) / 2;
    return (Rect){meter.x, meter.y + side * (bar + gap), meter.width, bar};
}

static void draw_meter(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    const Font *font = theme->fonts.small;
    int gap = theme->metrics.padding / 2;
    static const StringId SIDES[USB_SCENE_CHANNELS] = {STR_USB_DAC_LEFT, STR_USB_DAC_RIGHT};
    int label = text_width(font, i18n_text(STR_USB_DAC_LEFT));
    label = label > text_width(font, i18n_text(STR_USB_DAC_RIGHT))
                ? label
                : text_width(font, i18n_text(STR_USB_DAC_RIGHT));
    for (int side = 0; side < USB_SCENE_CHANNELS; side++) {
        Rect row = usb_screen_meter_bar(theme, &painter->layout, side);
        if (!usb_screen_touches(painter, row)) {
            continue;
        }
        Rect name = label_centered_area(font, (Rect){row.x, row.y, label, row.height},
                                        i18n_text(SIDES[side]));
        usb_screen_text(painter, name, (TextStyle){font, theme->colors.text_secondary, false},
                        i18n_text(SIDES[side]));
        Rect segments = {row.x + label + gap, row.y, row.width - label - gap, row.height};
        draw_bar(painter, segments, painter->scene->dac.level[side]);
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

static int draw_badge(const UsbPainter *painter, Rect area)
{
    const Theme *theme = painter->theme;
    const ThemeColors *colors = &theme->colors;
    char text[USB_SCREEN_TEXT_CAPACITY];
    rate_text(text, &painter->scene->dac);
    const Font *font = theme->fonts.title;
    int inset = theme->metrics.padding / 2;
    if (text_width(font, text) + 2 * inset + 4 > area.width) {
        font = theme->fonts.small;
    }
    int width = text_width(font, text) + 2 * inset;
    Rect frame = {area.x, area.y, width < area.width ? width : area.width,
                  font_line_height(font) + inset};
    if (!usb_screen_touches(painter, frame)) {
        return frame.y + frame.height;
    }
    canvas_fill_rect(painter->canvas, frame, colors->accent);
    Rect glow = {frame.x + 1, frame.y + 1, frame.width - 2, frame.height - 2};
    canvas_fill_rect(painter->canvas, glow, usb_screen_tint(colors->accent, colors->backdrop_top));
    Rect inside = {glow.x + 1, glow.y + 1, glow.width - 2, glow.height - 2};
    canvas_fill_rect(painter->canvas, inside, colors->backdrop_top);
    Rect words = {inside.x + inset - 2, inside.y, inside.width - 2 * inset + 4, inside.height};
    usb_screen_text(painter, label_centered_area(font, words, text),
                    (TextStyle){font, usb_screen_tint(colors->accent, colors->text), false}, text);
    return frame.y + frame.height;
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
    int top = theme->metrics.header_height;
    Rect area = {0, top, theme->metrics.screen_width,
                 painter->layout.detail.y - theme->metrics.padding - top};
    canvas_fill_rect(painter->canvas, area, theme->colors.background);
    list_view_draw(painter->scene->menu, painter->canvas, area, hal_clock_milliseconds());
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
    const char *state = i18n_text(dac_state_text(painter->scene));
    char named[USB_SCREEN_TEXT_CAPACITY];
    if (painter->scene->state == USB_SCENE_DAC_CHOOSE) {
        usb_screen_format_one(named, STR_USB_DAC_PICK, FIRMWARE_SHORT_NAME);
        state = named;
    }
    Rect lines = usb_screen_lines_below(painter, usb_screen_draw_state(painter, state));
    if (painter->scene->dac.bits != 0) {
        int below = draw_badge(painter, lines);
        lines = usb_screen_lines_below(painter, below);
    }
    usb_screen_service(painter->scene);
    draw_rows(painter, lines);
}
