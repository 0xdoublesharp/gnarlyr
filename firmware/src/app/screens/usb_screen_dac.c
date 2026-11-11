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
    SECONDS_PER_MINUTE = 60,
    SECONDS_PER_HOUR = 3600,
    GRID_COLUMNS = 2
};

typedef struct DacCell {
    StringId name;
    char value[USB_SCREEN_TEXT_CAPACITY];
} DacCell;

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
        usb_screen_text(painter, name, (TextStyle){font, theme->colors.text_secondary},
                        i18n_text(SIDES[side]));
        Rect segments = {row.x + label + gap, row.y, row.width - label - gap, row.height};
        draw_bar(painter, segments, painter->scene->dac.level[side]);
    }
}

static void number_text(char *text, uint32_t value)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_NUMBER_CAPACITY);
    string_builder_append_unsigned(&builder, value);
}

static void signed_text(char *text, int32_t value)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_NUMBER_CAPACITY);
    string_builder_append_char(&builder, value < 0 ? '-' : '+');
    string_builder_append_unsigned(&builder, (uint32_t)(value < 0 ? -value : value));
}

static void format_one(char *text, StringId id, const char *argument)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {argument};
    i18n_format(&builder, id, arguments, 1);
}

static void rate_text(char *text, const UsbSceneDac *dac)
{
    char rate[USB_SCREEN_NUMBER_CAPACITY];
    char bits[USB_SCREEN_NUMBER_CAPACITY];
    StringBuilder digits = string_builder_make(rate, sizeof rate);
    usb_screen_append_tenths(&digits, dac->rate / HZ_TENTH);
    if (dac->rate % HZ_PER_KHZ == 0) {
        number_text(rate, dac->rate / HZ_PER_KHZ);
    }
    number_text(bits, dac->bits);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {rate, bits};
    i18n_format(&builder, STR_USB_DAC_RATE, arguments, 2);
}

static void host_volume_text(char *text, const UsbSceneDac *dac)
{
    if (dac->host_muted) {
        format_one(text, STR_USB_DAC_MUTED_VALUE, "");
        return;
    }
    char number[USB_SCREEN_NUMBER_CAPACITY];
    int32_t db = dac->host_volume / DB_STEPS;
    signed_text(number, db);
    if (db == 0) {
        number_text(number, 0);
    }
    format_one(text, STR_EQ_GAIN, number);
}

static void time_text(char *text, uint32_t seconds)
{
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    uint32_t hours = seconds / SECONDS_PER_HOUR;
    uint32_t minutes = seconds / SECONDS_PER_MINUTE % SECONDS_PER_MINUTE;
    if (hours > 0) {
        string_builder_append_unsigned(&builder, hours);
        string_builder_append_char(&builder, ':');
        string_builder_append_char(&builder, (char)('0' + minutes / 10));
        string_builder_append_char(&builder, (char)('0' + minutes % 10));
    } else {
        string_builder_append_unsigned(&builder, minutes);
    }
    string_builder_append_char(&builder, ':');
    string_builder_append_char(&builder, (char)('0' + seconds % SECONDS_PER_MINUTE / 10));
    string_builder_append_char(&builder, (char)('0' + seconds % 10));
}

static StringId jack_text(uint8_t jack)
{
    if (jack == HAL_AUDIO_JACK_BALANCED) {
        return STR_JACK_44;
    }
    return jack == HAL_AUDIO_JACK_SINGLE_ENDED ? STR_JACK_35 : STR_ABOUT_NONE;
}

static void fill_cells(const UsbSceneDac *dac, bool high_speed, DacCell *cells)
{
    char number[USB_SCREEN_NUMBER_CAPACITY];
    cells[0].name = STR_VOLUME;
    number_text(cells[0].value, dac->volume);
    cells[1].name = STR_USB_DAC_HOST;
    host_volume_text(cells[1].value, dac);
    cells[2].name = STR_OUTPUT;
    format_one(cells[2].value, jack_text(dac->jack), "");
    cells[3].name = STR_USB_DAC_BUFFER;
    number_text(number, dac->buffer_ms);
    format_one(cells[3].value, STR_USB_DAC_MS, number);
    cells[4].name = STR_USB_DAC_CLOCK;
    signed_text(number, dac->drift_ppm);
    format_one(cells[4].value, STR_USB_DAC_PPM, number);
    cells[5].name = STR_USB_DAC_UNDERRUNS;
    number_text(cells[5].value, dac->underruns);
    cells[6].name = STR_USB_DAC_TIME;
    time_text(cells[6].value, dac->seconds);
    cells[7].name = STR_USB;
    format_one(cells[7].value, high_speed ? STR_USB_HIGH_SPEED : STR_USB_FULL_SPEED, "");
}

static int grid_columns(const UsbPainter *painter)
{
    return theme_is_portrait(painter->theme) ? 1 : GRID_COLUMNS;
}

static void draw_cells(const UsbPainter *painter, Rect area, const DacCell *cells, int count)
{
    int columns = grid_columns(painter);
    int gap = painter->theme->metrics.padding;
    int line = font_line_height(painter->theme->fonts.small);
    int width = (area.width - (columns - 1) * gap) / columns;
    for (int index = 0; index < count; index++) {
        Rect cell = {area.x + index % columns * (width + gap), area.y + index / columns * line,
                     width, line};
        if (cell.y + line > area.y + area.height) {
            return;
        }
        usb_screen_row(painter, cell, i18n_text(cells[index].name), cells[index].value);
    }
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

static void draw_format(const UsbPainter *painter, Rect row)
{
    const Theme *theme = painter->theme;
    char text[USB_SCREEN_TEXT_CAPACITY];
    if (painter->scene->dac.bits == 0) {
        return;
    }
    rate_text(text, &painter->scene->dac);
    usb_screen_text(
        painter, row,
        (TextStyle){theme->fonts.small, usb_screen_tint(theme->colors.accent, theme->colors.text)},
        text);
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
    if (painter->scene->menu != NULL) {
        draw_menu(painter);
        return;
    }
    if (usb_screen_touches(painter, painter->layout.meter)) {
        draw_meter(painter);
    }
    if (!usb_screen_touches(painter, usb_screen_column(&painter->layout))) {
        return;
    }
    const char *state = i18n_text(dac_state_text(painter->scene));
    char named[USB_SCREEN_TEXT_CAPACITY];
    if (painter->scene->state == USB_SCENE_DAC_CHOOSE) {
        format_one(named, STR_USB_DAC_PICK, FIRMWARE_SHORT_NAME);
        state = named;
    }
    Rect lines = usb_screen_lines_below(painter, usb_screen_draw_state(painter, state));
    int line = font_line_height(painter->theme->fonts.small);
    draw_format(painter, (Rect){lines.x, lines.y, lines.width, line});
    lines.y += line;
    lines.height -= line;
    DacCell cells[8];
    fill_cells(&painter->scene->dac, painter->scene->high_speed, cells);
    draw_cells(painter, lines, cells, (int)(sizeof cells / sizeof cells[0]));
}
