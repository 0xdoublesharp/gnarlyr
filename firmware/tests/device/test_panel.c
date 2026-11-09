#include "check.h"
#include "device/panel.h"

enum {
    LOG_CAPACITY = 512
};

typedef enum EntryKind {
    ENTRY_COMMAND,
    ENTRY_DATA,
    ENTRY_DELAY
} EntryKind;

typedef struct Entry {
    EntryKind kind;
    uint32_t value;
} Entry;

typedef struct Recorder {
    Entry entries[LOG_CAPACITY];
    int count;
} Recorder;

static void record(Recorder *recorder, EntryKind kind, uint32_t value)
{
    if (recorder->count < LOG_CAPACITY) {
        recorder->entries[recorder->count++] = (Entry){kind, value};
    }
}

static void record_command(void *context, uint8_t command)
{
    record(context, ENTRY_COMMAND, command);
}

static void record_data(void *context, uint8_t value)
{
    record(context, ENTRY_DATA, value);
}

static void record_pixels(void *context, const uint16_t *pixels, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        record(context, ENTRY_DATA, (uint8_t)(pixels[index] >> 8));
        record(context, ENTRY_DATA, (uint8_t)pixels[index]);
    }
}

static void record_delay(void *context, uint32_t milliseconds)
{
    record(context, ENTRY_DELAY, milliseconds);
}

static PanelBus recording_bus(Recorder *recorder)
{
    *recorder = (Recorder){.count = 0};
    return (PanelBus){record_command, record_data, record_pixels, record_delay, recorder};
}

#define C(value) {ENTRY_COMMAND, value}
#define D(value) {ENTRY_DATA, value}
#define W(value) {ENTRY_DELAY, value}

static void check_log(const Recorder *recorder, const Entry *expected, int count)
{
    CHECK_EQUAL(count, recorder->count);
    for (int index = 0; index < count && index < recorder->count; index++) {
        CHECK_EQUAL(expected[index].kind, recorder->entries[index].kind);
        CHECK_EQUAL(expected[index].value, recorder->entries[index].value);
    }
}

static const Entry ST7796S_INIT[] = {
    C(0x11), W(120),  C(0xF0), D(0xC3), C(0xF0), D(0x96), C(0x36), D(PANEL_ECHO_MADCTL),
    C(0x3A), D(0x55), C(0xB4), D(0x01), C(0xB6), D(0x20), D(0x02), D(0x3B),
    C(0xB7), D(0xC6), C(0xB9), D(0x02), D(0xE0), C(0xC0), D(0xC0), D(0x64),
    C(0xC1), D(0x1D), C(0xC2), D(0xA7), C(0xC5), D(0x28), C(0xE8), D(0x40),
    D(0x8A), D(0x00), D(0x00), D(0x29), D(0x19), D(0xA5), D(0x33), C(0xE0),
    D(0xF0), D(0x0B), D(0x12), D(0x09), D(0x0A), D(0x26), D(0x39), D(0x54),
    D(0x4E), D(0x38), D(0x13), D(0x13), D(0x2E), D(0x34), C(0xE1), D(0xF0),
    D(0x10), D(0x15), D(0x0D), D(0x0C), D(0x07), D(0x38), D(0x43), D(0x4D),
    D(0x3A), D(0x16), D(0x15), D(0x30), D(0x35), C(0xF0), D(0x3C), C(0xF0),
    D(0x69), C(0x35), D(0x00), C(0x29), C(0x21), C(0x2A), D(0x00), D(0x31),
    D(0x01), D(0x0E), C(0x2B), D(0x00), D(0x00), D(0x01), D(0xDF), C(0x2C),
};

static void the_st7796s_sends_its_init_stream(void)
{
    Recorder recorder;
    PanelBus bus = recording_bus(&recorder);
    panel_send_init(&bus);
    check_log(&recorder, ST7796S_INIT, (int)(sizeof ST7796S_INIT / sizeof ST7796S_INIT[0]));
}

static void the_echo_memory_access_byte_is_0x28_or_the_mirrored_0x68(void)
{
    CHECK(PANEL_ECHO_MADCTL == 0x28 || PANEL_ECHO_MADCTL == 0x68);
}

static void the_st7796s_init_sleeps_out_before_anything_else_and_waits_120_ms(void)
{
    Recorder recorder;
    PanelBus bus = recording_bus(&recorder);
    panel_send_init(&bus);
    CHECK_EQUAL(ENTRY_COMMAND, recorder.entries[0].kind);
    CHECK_EQUAL(0x11, recorder.entries[0].value);
    CHECK_EQUAL(ENTRY_DELAY, recorder.entries[1].kind);
    CHECK_EQUAL(120, recorder.entries[1].value);
}

static void an_echo_window_maps_visible_rows_to_panel_rows_from_49(void)
{
    static const Entry expected[] = {
        C(0x2A), D(0x00), D(0x00), D(0x01), D(0xDF), C(0x2B),
        D(0x00), D(0x31), D(0x01), D(0x0E), C(0x2C),
    };
    Recorder recorder;
    PanelBus bus = recording_bus(&recorder);
    panel_begin_write(&bus, (PanelWindow){0, 0, 480, 222});
    check_log(&recorder, expected, (int)(sizeof expected / sizeof expected[0]));
}

static void a_partial_echo_window_keeps_columns_plain_and_offsets_rows(void)
{
    static const Entry expected[] = {
        C(0x2A), D(0x00), D(0x0A), D(0x01), D(0x09), C(0x2B),
        D(0x00), D(0x5B), D(0x00), D(0x7C), C(0x2C),
    };
    Recorder recorder;
    PanelBus bus = recording_bus(&recorder);
    panel_begin_write(&bus, (PanelWindow){10, 42, 256, 34});
    check_log(&recorder, expected, (int)(sizeof expected / sizeof expected[0]));
}

static void the_board_default_window_offset_follows_the_board(void)
{
    CHECK_EQUAL(49, PANEL_FIRST_VISIBLE_ROW);
}

static void pixels_are_sent_high_byte_first(void)
{
    static const uint16_t pixels[] = {0xF800, 0x1234};
    static const Entry expected[] = {D(0xF8), D(0x00), D(0x12), D(0x34)};
    Recorder recorder;
    PanelBus bus = recording_bus(&recorder);
    bus.pixels(bus.context, pixels, 2);
    check_log(&recorder, expected, 4);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_st7796s_sends_its_init_stream),
        TEST_CASE(the_echo_memory_access_byte_is_0x28_or_the_mirrored_0x68),
        TEST_CASE(the_st7796s_init_sleeps_out_before_anything_else_and_waits_120_ms),
        TEST_CASE(an_echo_window_maps_visible_rows_to_panel_rows_from_49),
        TEST_CASE(a_partial_echo_window_keeps_columns_plain_and_offsets_rows),
        TEST_CASE(the_board_default_window_offset_follows_the_board),
        TEST_CASE(pixels_are_sent_high_byte_first),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
