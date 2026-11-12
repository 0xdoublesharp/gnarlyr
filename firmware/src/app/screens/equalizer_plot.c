#include "equalizer_plot.h"

#include "core/i18n.h"
#include "ui/label.h"

enum {
    NUMBER_CAPACITY = 24,
    CAPTION_CAPACITY = 64,
    LOG_BITS = 8,
    LOG_UNIT = 1 << LOG_BITS,
    LOG_NORMAL_BITS = 16
};

static const char *const BAND_LABELS[SETTINGS_EQUALIZER_BANDS] = {
    "31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};

const char *equalizer_plot_band_name(int band)
{
    return BAND_LABELS[band];
}

static void decimal(char *text, int tenth_db)
{
    StringBuilder builder = string_builder_make(text, NUMBER_CAPACITY);
    int magnitude = tenth_db < 0 ? -tenth_db : tenth_db;
    if (tenth_db != 0) {
        string_builder_append_char(&builder, tenth_db < 0 ? '-' : '+');
    }
    string_builder_append_unsigned(&builder, (uint32_t)(magnitude / 10));
    string_builder_append_char(&builder, '.');
    string_builder_append_unsigned(&builder, (uint32_t)(magnitude % 10));
}

void equalizer_plot_gain(char *text, size_t capacity, int tenth_db)
{
    char number[NUMBER_CAPACITY];
    decimal(number, tenth_db);
    StringBuilder builder = string_builder_make(text, capacity);
    i18n_format_text(&builder, STR_EQ_GAIN, number);
}

EqualizerPlotLayout equalizer_plot_layout(const Theme *theme, const char *hint, bool browser)
{
    int pad = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * pad;
    int small = font_line_height(theme->fonts.small);
    int title = font_line_height(theme->fonts.title);
    int hint_height = label_wrap_count(theme->fonts.small, hint, width) * small;
    int bottom = theme->metrics.screen_height - pad - hint_height - pad;
    int top = theme->metrics.header_height + pad;
    EqualizerPlotLayout layout = {.hint = {pad, bottom + pad, width, hint_height}};
    if (theme_is_portrait(theme)) {
        int list_height = browser ? 3 * theme->metrics.row_height : 0;
        layout.list = (Rect){pad, top, width, list_height};
        int chosen = top + list_height + (browser ? pad : 0);
        layout.primary = (Rect){pad, chosen, width, title};
        layout.secondary = (Rect){pad, chosen + title + pad / 2, width, title};
        int chart = browser ? chosen + title + pad : layout.secondary.y + title + pad;
        layout.preamp = (Rect){pad, bottom - 2 * small, width, small};
        layout.reference = (Rect){pad, bottom - small, width, small};
        layout.plot = (Rect){pad, chart, width, layout.preamp.y - pad - chart};
        return layout;
    }
    int left = width / 3;
    int right = pad + left + 2 * pad;
    int chart_width = width - left - 2 * pad;
    layout.preamp = (Rect){pad, bottom - small, left, small};
    layout.list = (Rect){pad, top, left, layout.preamp.y - top - pad};
    layout.primary = (Rect){browser ? right : pad, top, browser ? chart_width : left, title};
    layout.secondary = (Rect){pad, top + title + pad / 2, left, title};
    layout.reference = (Rect){right, bottom - small, chart_width, small};
    int chart = browser ? top + title + pad : top;
    layout.plot = (Rect){right, chart, chart_width, layout.reference.y - pad / 2 - chart};
    return layout;
}

static int logarithm(uint16_t hz)
{
    uint64_t value = (uint64_t)hz << LOG_NORMAL_BITS;
    int result = 0;
    while (value >= (UINT64_C(2) << LOG_NORMAL_BITS)) {
        value >>= 1;
        result += LOG_UNIT;
    }
    for (int bit = LOG_UNIT / 2; bit > 0; bit >>= 1) {
        value = value * value >> LOG_NORMAL_BITS;
        if (value >= (UINT64_C(2) << LOG_NORMAL_BITS)) {
            value >>= 1;
            result += bit;
        }
    }
    return result;
}

static int frequency_x(Rect area, uint16_t hz)
{
    int low = logarithm(PARAMETRIC_MIN_HZ);
    int high = logarithm(PARAMETRIC_MAX_HZ);
    return area.x + (logarithm(hz) - low) * (area.width - 1) / (high - low);
}

static Point response_point(Rect area, const EqualizerResponse *response, int point)
{
    int half = (area.height - 1) / 2;
    int gain = response->gain_tenth_db[point];
    uint16_t hz = response->frequency_hz[point] != 0 ? response->frequency_hz[point]
                                                     : equalizer_response_frequency(point);
    return (Point){frequency_x(area, hz), area.y + half - gain * half / response->range_tenth_db};
}

static void segment(Canvas *canvas, Point from, Point to, Color color)
{
    int dx = to.x - from.x;
    int dy = to.y >= from.y ? to.y - from.y : from.y - to.y;
    int step = to.y >= from.y ? 1 : -1;
    int error = dx - dy;
    for (;;) {
        canvas_fill_rect(canvas, (Rect){from.x, from.y, 1, 1}, color);
        if (from.x == to.x && from.y == to.y) {
            return;
        }
        int twice = 2 * error;
        if (twice > -dy) {
            error -= dy;
            from.x++;
        }
        if (twice < dx) {
            error += dx;
            from.y += step;
        }
    }
}

static void frequency_label(Canvas *canvas, const Theme *theme, Rect area, int band)
{
    const char *text = BAND_LABELS[band];
    int width = text_width(theme->fonts.small, text);
    int x = frequency_x(area, SETTINGS_EQUALIZER_BAND_HZ[band]) - width / 2;
    x = x < area.x ? area.x : x;
    x = x + width > area.x + area.width ? area.x + area.width - width : x;
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_tertiary},
               (Label){{x, area.y, width, area.height}, text});
}

static void selected_band(Canvas *canvas, const Theme *theme, Rect area, EqualizerPlotView view)
{
    if (view.selected_band < 0 || view.selected_band >= SETTINGS_EQUALIZER_BANDS) {
        return;
    }
    uint16_t hz = SETTINGS_EQUALIZER_BAND_HZ[view.selected_band];
    int point = 0;
    while (point < EQUALIZER_RESPONSE_POINTS - 1 && equalizer_response_frequency(point) < hz) {
        point++;
    }
    Point selected = response_point(area, view.response, point);
    Color ink = view.adjusting ? theme->colors.accent_alt : theme->colors.accent;
    canvas_fill_rect(canvas, (Rect){selected.x, area.y, 1, area.height}, theme->colors.divider);
    int edge = theme->metrics.scrollbar_inset;
    canvas_fill_rect(canvas,
                     (Rect){selected.x - edge, selected.y - edge, 2 * edge + 1, 2 * edge + 1}, ink);
}

void equalizer_plot_draw(Canvas *canvas, const Theme *theme, Rect area, EqualizerPlotView view)
{
    int small = font_line_height(theme->fonts.small);
    char high[NUMBER_CAPACITY];
    char low[NUMBER_CAPACITY];
    equalizer_plot_gain(high, sizeof high, view.response->range_tenth_db);
    equalizer_plot_gain(low, sizeof low, -view.response->range_tenth_db);
    int axis = text_width(theme->fonts.small, low) + theme->metrics.padding / 2;
    Rect chart = {area.x + axis, area.y, area.width - axis, area.height - small};
    TextStyle scale = {theme->fonts.small, theme->colors.text_tertiary};
    label_draw(canvas, scale, (Label){{area.x, chart.y, axis, small}, high});
    label_draw(canvas, scale, (Label){{area.x, chart.y + chart.height - small, axis, small}, low});
    if (chart.height >= 3 * small) {
        char zero[NUMBER_CAPACITY];
        equalizer_plot_gain(zero, sizeof zero, 0);
        label_draw(canvas, scale,
                   (Label){{area.x, chart.y + (chart.height - small) / 2, axis, small}, zero});
    }
    Canvas clipped = canvas_clipped(canvas, chart);
    int half = (chart.height - 1) / 2;
    canvas_fill_rect(&clipped, (Rect){chart.x, chart.y + half, chart.width, 1},
                     theme->colors.text_tertiary);
    canvas_fill_rect(&clipped, (Rect){chart.x, chart.y + half / 2, chart.width, 1},
                     theme->colors.divider);
    canvas_fill_rect(&clipped, (Rect){chart.x, chart.y + half + half / 2, chart.width, 1},
                     theme->colors.divider);
    for (int point = 1; point < EQUALIZER_RESPONSE_POINTS; point++) {
        segment(&clipped, response_point(chart, view.response, point - 1),
                response_point(chart, view.response, point), theme->colors.accent);
    }
    selected_band(&clipped, theme, chart, view);
    Rect labels = {chart.x, area.y + area.height - small, chart.width, small};
    frequency_label(canvas, theme, labels, 0);
    if (labels.width >= 15 * theme->metrics.padding) {
        frequency_label(canvas, theme, labels, 5);
    }
    frequency_label(canvas, theme, labels, 9);
}

void equalizer_plot_caption(Canvas *canvas, const Theme *theme, Rect area,
                            const EqualizerResponse *response)
{
    char number[NUMBER_CAPACITY];
    char caption[CAPTION_CAPACITY];
    decimal(number, response->preamp_tenth_db);
    StringBuilder builder = string_builder_make(caption, sizeof caption);
    if (response->muted) {
        string_builder_append(&builder, i18n_text(STR_PEQ_MUTED));
    } else {
        i18n_format_text(&builder, STR_EQ_PREAMP, number);
    }
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
               (Label){area, caption});
}
