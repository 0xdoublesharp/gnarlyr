#include "usb_screen_parts.h"

#include "core/i18n.h"
#include "hal/audio.h"
#include "ui/label.h"

enum {
    ROW_VALUE,
    ROW_HEADING,
    ROW_SECTION,
    SECTOR_BYTES = 512,
    BYTES_PER_KB = 1000,
    DB_STEPS = 256,
    TENTHS = 10,
    FIXED_ONE = 65536,
    FEEDBACK_PLACES = 10000,
    HEX_DIGIT = 16,
    WIDE_COLUMNS = 2
};

typedef enum StatField {
    FIELD_NONE,
    FIELD_READ,
    FIELD_WRITTEN,
    FIELD_COMMANDS,
    FIELD_PER_COMMAND,
    FIELD_FREE,
    FIELD_PEAK,
    FIELD_AVERAGE,
    FIELD_BUSY,
    FIELD_ERRORS,
    FIELD_LINK,
    FIELD_TIME,
    FIELD_FORMAT,
    FIELD_INTERFACE,
    FIELD_BUFFER,
    FIELD_CLOCK,
    FIELD_FEEDBACK,
    FIELD_UNDERRUNS,
    FIELD_OVERRUNS,
    FIELD_OUTPUT,
    FIELD_GAIN,
    FIELD_HOST,
    FIELD_DATA,
    FIELD_DAC_TIME
} StatField;

typedef struct StatSpec {
    uint8_t kind;
    uint8_t lun;
    uint8_t field;
    StringId name;
} StatSpec;

typedef struct StatTable {
    const StatSpec *rows;
    int count;
} StatTable;

typedef struct StatPlace {
    int page;
    int column;
    int line;
} StatPlace;

typedef struct StatGrid {
    Rect area;
    int columns;
    int lines;
    int width;
    int line;
} StatGrid;

typedef void (*StatFill)(const UsbPainter *painter, uint8_t lun, char *text);

#define DRIVE_ROWS(lun, title)                                                                     \
    {ROW_HEADING, lun, FIELD_NONE, title}, {ROW_VALUE, lun, FIELD_READ, STR_USB_STAT_READ},        \
        {ROW_VALUE, lun, FIELD_WRITTEN, STR_USB_STAT_WRITTEN},                                     \
        {ROW_VALUE, lun, FIELD_COMMANDS, STR_USB_STAT_COMMANDS},                                   \
        {ROW_VALUE, lun, FIELD_PER_COMMAND, STR_USB_STAT_PER_COMMAND},                             \
    {                                                                                              \
        ROW_VALUE, lun, FIELD_FREE, STR_USB_STAT_FREE                                              \
    }

static const StatSpec STORAGE_ROWS[] = {
    DRIVE_ROWS(0, STR_USB_STAT_INTERNAL),
    DRIVE_ROWS(1, STR_USB_STAT_CARD),
    {ROW_SECTION, 0, FIELD_PEAK, STR_USB_STAT_PEAK},
    {ROW_VALUE, 0, FIELD_AVERAGE, STR_USB_STAT_AVERAGE},
    {ROW_VALUE, 0, FIELD_BUSY, STR_USB_STAT_BUSY},
    {ROW_VALUE, 0, FIELD_ERRORS, STR_USB_STAT_ERRORS},
    {ROW_VALUE, 0, FIELD_LINK, STR_USB_STAT_LINK},
    {ROW_VALUE, 0, FIELD_TIME, STR_USB_DAC_TIME},
};

static const StatSpec DAC_ROWS[] = {
    {ROW_SECTION, 0, FIELD_FORMAT, STR_USB_DAC_FORMAT},
    {ROW_VALUE, 0, FIELD_INTERFACE, STR_USB_DAC_INTERFACE},
    {ROW_VALUE, 0, FIELD_BUFFER, STR_USB_DAC_BUFFER},
    {ROW_VALUE, 0, FIELD_CLOCK, STR_USB_DAC_CLOCK},
    {ROW_VALUE, 0, FIELD_FEEDBACK, STR_USB_DAC_FEEDBACK},
    {ROW_VALUE, 0, FIELD_UNDERRUNS, STR_USB_DAC_UNDERRUNS},
    {ROW_VALUE, 0, FIELD_OVERRUNS, STR_USB_DAC_OVERRUNS},
    {ROW_VALUE, 0, FIELD_OUTPUT, STR_OUTPUT},
    {ROW_VALUE, 0, FIELD_GAIN, STR_GAIN},
    {ROW_VALUE, 0, FIELD_HOST, STR_USB_DAC_HOST},
    {ROW_VALUE, 0, FIELD_DATA, STR_USB_DAC_DATA},
    {ROW_VALUE, 0, FIELD_DAC_TIME, STR_USB_DAC_TIME},
};

static const UsbSceneDrive *drive_of(const UsbPainter *painter, uint8_t lun)
{
    return &painter->scene->drives[lun < USB_SCENE_DRIVES ? lun : 0];
}

static const UsbSceneUsage *usage_of(const UsbPainter *painter, uint8_t lun)
{
    return &painter->scene->usage[lun < USB_SCENE_DRIVES ? lun : 0];
}

static void fill_read(const UsbPainter *painter, uint8_t lun, char *text)
{
    usb_screen_size_text(text, usage_of(painter, lun)->read_sectors);
}

static void fill_written(const UsbPainter *painter, uint8_t lun, char *text)
{
    usb_screen_size_text(text, usage_of(painter, lun)->written_sectors);
}

static void fill_commands(const UsbPainter *painter, uint8_t lun, char *text)
{
    const UsbSceneUsage *drive = usage_of(painter, lun);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    string_builder_append_unsigned(&builder, drive->reads);
    string_builder_append(&builder, " / ");
    string_builder_append_unsigned(&builder, drive->writes);
}

static void fill_per_command(const UsbPainter *painter, uint8_t lun, char *text)
{
    const UsbSceneUsage *drive = usage_of(painter, lun);
    uint32_t commands = drive->reads + drive->writes;
    uint64_t bytes = ((uint64_t)drive->read_sectors + drive->written_sectors) * SECTOR_BYTES;
    char number[USB_SCREEN_NUMBER_CAPACITY];
    uint64_t each = commands == 0 ? 0 : (bytes / commands + BYTES_PER_KB / 2) / BYTES_PER_KB;
    usb_screen_number_text(number, (uint32_t)each);
    usb_screen_format_one(text, STR_USB_KB, number);
}

static void fill_free(const UsbPainter *painter, uint8_t lun, char *text)
{
    uint32_t free = usage_of(painter, lun)->free_sectors;
    if (free == USB_SCENE_FREE_UNKNOWN) {
        usb_screen_format_one(text, STR_USB_UNKNOWN, "");
        return;
    }
    usb_screen_size_text(text, free);
}

static void fill_rate(char *text, uint32_t kilobytes_per_second)
{
    char number[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_rate_text(number, kilobytes_per_second);
    usb_screen_format_one(text, STR_USB_MB_S, number);
}

static void fill_peak(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    fill_rate(text, painter->scene->meter.peak);
}

static void fill_average(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    fill_rate(text, usb_meter_average(&painter->scene->meter));
}

static void fill_percent(char *text, uint32_t percent)
{
    char number[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(number, percent);
    usb_screen_format_one(text, STR_POWER_PERCENT, number);
}

static void fill_busy(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    fill_percent(text, painter->scene->link.busy_percent);
}

static char hex_digit(uint32_t value)
{
    return (char)(value < TENTHS ? '0' + value : 'A' + value - TENTHS);
}

static void append_hex(StringBuilder *builder, uint8_t value)
{
    string_builder_append_char(builder, hex_digit(value / HEX_DIGIT));
    string_builder_append_char(builder, hex_digit(value % HEX_DIGIT));
}

static void fill_errors(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    const UsbSceneLink *link = &painter->scene->link;
    if (link->errors == 0) {
        usb_screen_number_text(text, 0);
        return;
    }
    char count[USB_SCREEN_NUMBER_CAPACITY];
    char sense[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(count, link->errors);
    StringBuilder code = string_builder_make(sense, sizeof sense);
    append_hex(&code, link->sense_key);
    string_builder_append_char(&code, '/');
    append_hex(&code, link->sense_code);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {count, sense};
    i18n_format(&builder, STR_USB_ERRORS_VALUE, arguments, 2);
}

static void fill_link(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    char packet[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(packet, painter->scene->link.packet);
    usb_screen_format_one(text, painter->scene->high_speed ? STR_USB_LINK_HIGH : STR_USB_LINK_FULL,
                          packet);
}

static void fill_time(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    usb_screen_time_text(text, painter->scene->link.seconds);
}

static void fill_format(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    const UsbSceneDac *dac = &painter->scene->dac;
    char rate[USB_SCREEN_NUMBER_CAPACITY];
    char bits[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(rate, dac->rate);
    usb_screen_number_text(bits, dac->bits);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {rate, bits};
    i18n_format(&builder, STR_USB_DAC_EXACT, arguments, 2);
}

static void fill_interface(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    const UsbSceneDac *dac = &painter->scene->dac;
    char alt[USB_SCREEN_NUMBER_CAPACITY];
    char channels[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(alt, dac->alt);
    usb_screen_number_text(channels, dac->channels);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {alt, channels};
    i18n_format(&builder, dac->asynchronous ? STR_USB_DAC_UAC2 : STR_USB_DAC_UAC1, arguments, 2);
}

static void fill_buffer(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    const UsbSceneDac *dac = &painter->scene->dac;
    char average[USB_SCREEN_NUMBER_CAPACITY];
    char low[USB_SCREEN_NUMBER_CAPACITY];
    char high[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(average, dac->buffer_average_ms);
    usb_screen_number_text(low, dac->buffer_low_ms);
    usb_screen_number_text(high, dac->buffer_high_ms);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {average, low, high};
    i18n_format(&builder, STR_USB_DAC_BUFFER_RANGE, arguments, 3);
}

static void append_signed(StringBuilder *builder, int32_t value)
{
    string_builder_append_char(builder, value < 0 ? '-' : '+');
    string_builder_append_unsigned(builder, (uint32_t)(value < 0 ? -value : value));
}

static void fill_clock(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    char number[USB_SCREEN_NUMBER_CAPACITY];
    StringBuilder builder = string_builder_make(number, sizeof number);
    append_signed(&builder, painter->scene->dac.drift_ppm);
    usb_screen_format_one(text, STR_USB_DAC_PPM, number);
}

static void fill_feedback(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    uint32_t feedback = painter->scene->dac.feedback;
    if (!painter->scene->dac.asynchronous) {
        usb_screen_format_one(text, STR_USB_DAC_ADAPTIVE, "");
        return;
    }
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    uint32_t places = (uint32_t)((uint64_t)(feedback % FIXED_ONE) * FEEDBACK_PLACES / FIXED_ONE);
    string_builder_append_unsigned(&builder, feedback / FIXED_ONE);
    string_builder_append_char(&builder, '.');
    for (uint32_t digit = FEEDBACK_PLACES / TENTHS; digit > 0; digit /= TENTHS) {
        string_builder_append_char(&builder, (char)('0' + places / digit % TENTHS));
    }
}

static void fill_underruns(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    usb_screen_number_text(text, painter->scene->dac.underruns);
}

static void fill_overruns(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    usb_screen_number_text(text, painter->scene->dac.overruns);
}

static void fill_output(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    uint8_t jack = painter->scene->dac.jack;
    StringId id = jack == HAL_AUDIO_JACK_BALANCED ? STR_JACK_44 : STR_ABOUT_NONE;
    usb_screen_format_one(text, jack == HAL_AUDIO_JACK_SINGLE_ENDED ? STR_JACK_35 : id, "");
}

static void fill_gain(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    const UsbSceneDac *dac = &painter->scene->dac;
    char value[USB_SCREEN_NUMBER_CAPACITY];
    StringBuilder hex = string_builder_make(value, sizeof value);
    string_builder_append(&hex, "0x");
    append_hex(&hex, dac->gain_register);
    StringBuilder builder = string_builder_make(text, USB_SCREEN_TEXT_CAPACITY);
    const char *arguments[] = {i18n_text(dac->gain_high ? STR_GAIN_HIGH : STR_GAIN_LOW), value};
    i18n_format(&builder, STR_USB_DAC_GAIN_VALUE, arguments, 2);
}

static void fill_host(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    const UsbSceneDac *dac = &painter->scene->dac;
    if (dac->host_muted) {
        usb_screen_format_one(text, STR_USB_DAC_MUTED_VALUE, "");
        return;
    }
    int32_t tenths = dac->host_volume * TENTHS / DB_STEPS;
    uint32_t size = (uint32_t)(tenths < 0 ? -tenths : tenths);
    char number[USB_SCREEN_NUMBER_CAPACITY];
    StringBuilder builder = string_builder_make(number, sizeof number);
    string_builder_append_char(&builder, tenths < 0 ? '-' : '+');
    usb_screen_append_tenths(&builder, size);
    usb_screen_format_one(text, STR_EQ_GAIN, number);
}

static void fill_data(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    char number[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_number_text(number, painter->scene->dac.data_rate);
    usb_screen_format_one(text, STR_USB_KB_S, number);
}

static void fill_dac_time(const UsbPainter *painter, uint8_t lun, char *text)
{
    (void)lun;
    usb_screen_time_text(text, painter->scene->dac.seconds);
}

static const StatFill FILLS[] = {
    [FIELD_READ] = fill_read,           [FIELD_WRITTEN] = fill_written,
    [FIELD_COMMANDS] = fill_commands,   [FIELD_PER_COMMAND] = fill_per_command,
    [FIELD_FREE] = fill_free,           [FIELD_PEAK] = fill_peak,
    [FIELD_AVERAGE] = fill_average,     [FIELD_BUSY] = fill_busy,
    [FIELD_ERRORS] = fill_errors,       [FIELD_LINK] = fill_link,
    [FIELD_TIME] = fill_time,           [FIELD_FORMAT] = fill_format,
    [FIELD_INTERFACE] = fill_interface, [FIELD_BUFFER] = fill_buffer,
    [FIELD_CLOCK] = fill_clock,         [FIELD_FEEDBACK] = fill_feedback,
    [FIELD_UNDERRUNS] = fill_underruns, [FIELD_OVERRUNS] = fill_overruns,
    [FIELD_OUTPUT] = fill_output,       [FIELD_GAIN] = fill_gain,
    [FIELD_HOST] = fill_host,           [FIELD_DATA] = fill_data,
    [FIELD_DAC_TIME] = fill_dac_time,
};

static StatTable table_of(const UsbPainter *painter)
{
    if (painter->status.view == APP_USB_DAC) {
        return (StatTable){DAC_ROWS, (int)(sizeof DAC_ROWS / sizeof DAC_ROWS[0])};
    }
    return (StatTable){STORAGE_ROWS, (int)(sizeof STORAGE_ROWS / sizeof STORAGE_ROWS[0])};
}

static bool shown(const UsbPainter *painter, const StatSpec *spec)
{
    return spec->kind != ROW_VALUE || spec->field == FIELD_NONE || spec->field >= FIELD_PEAK ||
           drive_of(painter, spec->lun)->state != USB_SCENE_DRIVE_ABSENT;
}

static StatGrid grid_of(const UsbPainter *painter)
{
    const Theme *theme = painter->theme;
    StatGrid grid = {.area = usb_screen_content(theme, &painter->layout),
                     .columns = theme_is_portrait(theme) ? 1 : WIDE_COLUMNS,
                     .line = font_line_height(theme->fonts.small)};
    grid.lines = grid.area.height / grid.line;
    grid.lines = grid.lines < 1 ? 1 : grid.lines;
    grid.width = (grid.area.width - (grid.columns - 1) * theme->metrics.padding) / grid.columns;
    return grid;
}

static void next_column(StatPlace *place, const StatGrid *grid)
{
    place->line = 0;
    place->column++;
    if (place->column == grid->columns) {
        place->column = 0;
        place->page++;
    }
}

static int section_length(const UsbPainter *painter, StatTable table, int first)
{
    int length = 0;
    for (int index = first; index < table.count; index++) {
        if (index > first && table.rows[index].kind != ROW_VALUE) {
            break;
        }
        length += shown(painter, &table.rows[index]) ? 1 : 0;
    }
    return length;
}

static void place_row(const UsbPainter *painter, StatTable table, int index, StatPlace *place,
                      const StatGrid *grid)
{
    bool opens = table.rows[index].kind != ROW_VALUE;
    if (opens && place->line > 0 &&
        place->line + section_length(painter, table, index) > grid->lines) {
        next_column(place, grid);
    }
}

static void advance(StatPlace *place, const StatGrid *grid)
{
    place->line++;
    if (place->line == grid->lines) {
        next_column(place, grid);
    }
}

static void heading_note(const UsbPainter *painter, uint8_t lun, char *text)
{
    const UsbSceneDrive *drive = drive_of(painter, lun);
    text[0] = '\0';
    if (drive->state == USB_SCENE_DRIVE_ABSENT) {
        usb_screen_format_one(text, STR_USB_NO_CARD, "");
    } else if (drive->state == USB_SCENE_DRIVE_EJECTED) {
        usb_screen_format_one(text, STR_USB_EJECTED, "");
    }
}

static void draw_heading(const UsbPainter *painter, Rect row, const StatSpec *spec)
{
    const Theme *theme = painter->theme;
    char note[USB_SCREEN_TEXT_CAPACITY];
    heading_note(painter, spec->lun, note);
    Color ink = usb_screen_tint(theme->colors.accent, theme->colors.text);
    usb_screen_text(painter, (Rect){row.x, row.y, row.width, row.height},
                    (TextStyle){theme->fonts.small, ink}, i18n_text(spec->name));
    canvas_fill_rect(painter->canvas, (Rect){row.x, row.y + row.height - 1, row.width, 1},
                     usb_screen_tint(theme->colors.divider, theme->colors.accent));
    if (note[0] != '\0') {
        int width = text_width(theme->fonts.small, note);
        usb_screen_text(painter, (Rect){row.x + row.width - width, row.y, width, row.height},
                        (TextStyle){theme->fonts.small, theme->colors.text_secondary}, note);
    }
}

static void draw_row(const UsbPainter *painter, Rect row, const StatSpec *spec)
{
    if (spec->kind == ROW_HEADING) {
        draw_heading(painter, row, spec);
        return;
    }
    char value[USB_SCREEN_TEXT_CAPACITY];
    usb_screen_service(painter->scene);
    FILLS[spec->field](painter, spec->lun, value);
    usb_screen_row(painter, row, i18n_text(spec->name), value);
}

static Rect cell_at(const StatGrid *grid, StatPlace place, int gap)
{
    return (Rect){grid->area.x + place.column * (grid->width + gap),
                  grid->area.y + place.line * grid->line, grid->width, grid->line};
}

static int lay_out(const UsbPainter *painter, int page)
{
    StatTable table = table_of(painter);
    StatGrid grid = grid_of(painter);
    StatPlace place = {0, 0, 0};
    for (int index = 0; index < table.count; index++) {
        if (!shown(painter, &table.rows[index])) {
            continue;
        }
        place_row(painter, table, index, &place, &grid);
        Rect cell = cell_at(&grid, place, painter->theme->metrics.padding);
        if (place.page == page && painter->canvas != NULL && usb_screen_touches(painter, cell)) {
            draw_row(painter, cell, &table.rows[index]);
        }
        advance(&place, &grid);
    }
    return place.line == 0 && place.column == 0 ? place.page : place.page + 1;
}

int usb_screen_stats_pages(const UsbPainter *painter)
{
    return lay_out(painter, -1);
}

void usb_screen_draw_stats(const UsbPainter *painter)
{
    if (usb_screen_touches(painter, usb_screen_content(painter->theme, &painter->layout))) {
        lay_out(painter, painter->scene->page - 1);
    }
}
