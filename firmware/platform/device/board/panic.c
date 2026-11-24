#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "core/deadline.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "device/panic_text.h"
#include "hal/display.h"
#include "hal/system.h"
#include "panel_port.h"
#include "registers.h"
#include "usb.h"
#include "usb_report.h"

#define CRU_CLOCK_SELECT (CRU_BASE + 0x14u)

enum {
    SCALE = 2,
    GLYPH_WIDTH = PANIC_GLYPH_ADVANCE * SCALE,
    GLYPH_HEIGHT = PANIC_GLYPH_ROWS * SCALE,
    LEFT = 8,
    TOP = 8,
    LINE_HEIGHT = GLYPH_HEIGHT + 6,
    LINE_CAPACITY = 32,
    HEX_DIGITS = 8,
    MILLISECONDS_PER_SECOND = 1000,
    MICROSECONDS_PER_MILLISECOND = 1000,
    CRYSTAL_MHZ = 24,
    CYCLES_PER_LOOP = 3,
    POWER_HOLD_MS = 2000,
    STACKED_LR_WORD = 5,
    STACKED_PC_WORD = 6,
    FRAME_WORDS = 8,
    WORDS_PER_LINE = 3,
    WHITE = 0xFFFF,
    BLACK = 0x0000
};

static uint16_t glyph[GLYPH_WIDTH * GLYPH_HEIGHT];
static int line;
static bool panic_active;
static bool power_down;
static uint32_t power_down_since_ms;

bool board_panic_active(void)
{
    return panic_active;
}

static void delay_ms_systick(uint32_t milliseconds, uint32_t ticks_per_ms)
{
    uint32_t prev = register_read(SYSTICK_CURRENT);
    uint32_t rem = 0;
    while (milliseconds > 0) {
        uint32_t now = register_read(SYSTICK_CURRENT);
        uint32_t delta = now < prev ? prev - now : 0u;
        prev = now;
        rem += delta;
        if (rem >= ticks_per_ms) {
            rem -= ticks_per_ms;
            milliseconds--;
        }
    }
}

static void delay_ms_cycles(uint32_t milliseconds, uint32_t ticks_per_ms)
{
    uint32_t loops = ticks_per_ms / CYCLES_PER_LOOP;
    if (loops == 0) {
        loops = 1u;
    }
    while (milliseconds-- > 0) {
        uint32_t count = loops;
        __asm__ volatile("1: subs %0, #1\n\tbne 1b" : "+r"(count) : : "cc");
    }
}

CORE_COLD void board_panic_delay_ms(uint32_t milliseconds)
{
    uint32_t divider = (register_read(CRU_CLOCK_SELECT) & 7u) + 1u;
    uint32_t mhz = hal_system_clock_mhz();
    uint32_t ticks_per_ms = (mhz != 0 ? mhz : CRYSTAL_MHZ) * MICROSECONDS_PER_MILLISECOND / divider;
    if ((register_read(SYSTICK_CTRL) & 1u) != 0) {
        delay_ms_systick(milliseconds, ticks_per_ms);
    } else {
        delay_ms_cycles(milliseconds, ticks_per_ms);
    }
}

static void draw_line(const char *text)
{
    int x = LEFT;
    int y = TOP + line * LINE_HEIGHT;
    for (; *text != '\0' && x + GLYPH_WIDTH <= HAL_DISPLAY_WIDTH; text++, x += GLYPH_WIDTH) {
        panic_render_glyph(*text, SCALE, (PanicColors){WHITE, BLACK}, glyph);
        hal_display_write(x, y, GLYPH_WIDTH, GLYPH_HEIGHT, glyph);
    }
    line++;
}

static void draw_pair_line(const char *first_label, uint32_t first, const char *second_label,
                           uint32_t second)
{
    char text[LINE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, first_label);
    usb_report_append_hex(&builder, first, HEX_DIGITS);
    if (second_label != NULL) {
        string_builder_append_char(&builder, ' ');
        string_builder_append(&builder, second_label);
        usb_report_append_hex(&builder, second, HEX_DIGITS);
    }
    draw_line(text);
}

static void draw_run_line(void)
{
    BoardRunSnapshot run = board_start_record_snapshot();
    char text[LINE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, "UP ");
    string_builder_append_unsigned(&builder, run.uptime_ms / MILLISECONDS_PER_SECOND);
    string_builder_append(&builder, " STEP ");
    string_builder_append_unsigned(&builder, run.step);
    string_builder_append(&builder, run.step_finished ? " MV " : " U MV ");
    string_builder_append_unsigned(&builder, run.millivolts);
    draw_line(text);
}

static void start_screen(const char *title)
{
    panic_active = true;
    power_down = false;
    usb_silence_audio();
    board_panel_panic_reset();
    hal_display_set_rotation(HAL_DISPLAY_LANDSCAPE);
    board_display_clear();
    board_backlight_full();
    line = 0;
    draw_line(title);
}

CORE_SEAM void board_panic_stall(uint32_t pc, uint32_t lr)
{
    start_screen("STALL");
    draw_pair_line("PC ", pc, NULL, 0);
    draw_pair_line("LR ", lr, NULL, 0);
    draw_run_line();
}

static void draw_words_line(const uint32_t *words)
{
    char text[LINE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    for (int index = 0; index < WORDS_PER_LINE; index++) {
        if (index > 0) {
            string_builder_append_char(&builder, ' ');
        }
        usb_report_append_hex(&builder, words[index], HEX_DIGITS);
    }
    draw_line(text);
}

CORE_SEAM void board_panic_fault(const uint32_t *frame, uint32_t status)
{
    start_screen("FAULT");
    draw_pair_line("PC ", frame[STACKED_PC_WORD], "LR ", frame[STACKED_LR_WORD]);
    draw_pair_line("CFSR ", status, "SP ", (uint32_t)(uintptr_t)frame);
    draw_words_line(frame + FRAME_WORDS);
    draw_words_line(frame + FRAME_WORDS + WORDS_PER_LINE);
    draw_run_line();
}

CORE_COLD void board_panic_poll_power(uint32_t now_ms)
{
    if (!board_power_key_down()) {
        power_down = false;
        return;
    }
    if (!power_down) {
        power_down = true;
        power_down_since_ms = now_ms;
    }
    if (deadline_elapsed(now_ms, power_down_since_ms, POWER_HOLD_MS)) {
        board_power_cut();
    }
}

void hal_system_stall_test(void)
{
    for (;;) {
        __asm__ volatile("nop");
    }
}
