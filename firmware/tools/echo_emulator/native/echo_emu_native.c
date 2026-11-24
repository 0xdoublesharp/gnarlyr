#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unicorn/unicorn.h>

#define EMU_RAM_BASE 0x03000000u
#define EMU_RAM_SIZE 0x100000u
#define EMU_DATA_LOW_END 0x03048000u
#define EMU_CODE_WINDOW_END 0x03094000u
#define EMU_TRAMPOLINE 0x03FF0000u
#define EMU_CORE1_RAM_BASE 0x01000000u
#define EMU_CORE1_RAM_SIZE 0x60000u
#define EMU_CORE1_SHARED_BASE 0x01020000u
#define EMU_CORE1_SHARED_OFFSET 0x20000u
#define EMU_CORE1_SHARED_SIZE 0x40000u
#define EMU_CORE1_CODE_BASE 0x0u
#define EMU_CORE1_CODE_SIZE 0x20000u
#define EMU_CORE1_CODE_CACHE_LEN (EMU_CORE1_CODE_SIZE >> 1)
#define EMU_CORE1_CONTROL 0x01020100u
#define EMU_CORE1_CONTROL_BYTES 128u
#define EMU_CORE1_SAMPLES 0x0102E000u
#define EMU_CORE1_RING 0x01040000u
#define EMU_CORE1_SAMPLE_BYTES (EMU_CORE1_RING - EMU_CORE1_SAMPLES)
#define EMU_CORE1_SHARED_CYCLES 0x0102005Cu
#define EMU_CORE1_SHARED_PLL_MHZ 0x01020060u
#define EMU_CORE1_WRITTEN_OFFSET 40u
#define EMU_CORE1_SCB_BASE 0xE0000000u
#define EMU_CORE1_SCB_SIZE 0x100000u
#define EMU_CORE1_PERIPH_BASE 0x40000000u
#define EMU_CORE1_PERIPH_SIZE 0x200000u
#define EMU_CORE1_CRU_CLOCK_SELECT 0x40180014u
#define EMU_CORE1_SYSTICK_RELOAD 0xE000E014u
#define EMU_CORE1_SYSTICK_CURRENT 0xE000E018u
#define EMU_CORE1_PLL_MHZ 200u
#define EMU_CORE1_WAKES_PER_STEP 64
#define EMU_CORE1_RUN_INSN_LIMIT 100000000ULL

#define EMU_SLICE_INSTRUCTIONS 4000ULL
#define EMU_MASKED_SLICE_INSTRUCTIONS 5ULL
#define EMU_MAX_RUN_BUDGET 200000ULL
#define EMU_SYSTICK_POLL_JUMP_CYCLES 240ULL
#define EMU_SYSTICK_VECTOR_INDEX 15u

#define EMU_SYSTICK_CTRL 0x10u
#define EMU_SYSTICK_RELOAD 0x14u
#define EMU_SYSTICK_CURRENT 0x18u
#define EMU_SCB_ICSR 0xD04u
#define EMU_SCB_VTOR 0xD08u
#define EMU_SCB_AIRCR 0xD0Cu
#define EMU_ICSR_PENDSTSET (1u << 26)
#define EMU_ICSR_PENDSTCLR (1u << 25)
#define EMU_AIRCR_VECTKEY 0x05FA0000u
#define EMU_AIRCR_SYSRESETREQ 0x4u
#define EMU_SYSTICK_TICKINT 0x2u

#define EMU_GPIO_DATA 0x00u
#define EMU_GPIO_DIRECTION 0x04u
#define EMU_GPIO_INPUT_LEVEL 0x50u
#define EMU_POWER_KEY_PORT 2u
#define EMU_POWER_KEY_PIN 5u
#define EMU_CHARGE_PORT 2u
#define EMU_CHARGE_PIN 3u
#define EMU_CARD_DETECT_PORT 0u
#define EMU_CARD_DETECT_PIN 11u
#define EMU_BALANCED_PORT 2u
#define EMU_BALANCED_PIN 8u

#define EMU_GRF_VBUS 0xE8u
#define EMU_GRF_VBUS_PRESENT (1u << 1)

#define EMU_ADC_RESULT 0x00u
#define EMU_ADC_CONTROL 0x08u
#define EMU_ADC_KEY_CHANNEL 1u

#define EMU_VOP_CONTROL 0x00u
#define EMU_VOP_STATUS 0x24u
#define EMU_VOP_COMMAND 0x28u
#define EMU_VOP_DATA 0x2Cu
#define EMU_VOP_CONTROL_PIXELS 0x2u

#define EMU_DMAC_COMP_TYPE 0x3F8u
#define EMU_DMAC_CHANNEL_ENABLE 0x3A0u
#define EMU_DMAC_PUSH_BIT 0x01u

#define EMU_CASET 0x2Au
#define EMU_RASET 0x2Bu
#define EMU_RAMWR 0x2Cu
#define EMU_PANEL_WIDTH 480
#define EMU_PANEL_HEIGHT 222
#define EMU_PANEL_PIXELS (EMU_PANEL_WIDTH * EMU_PANEL_HEIGHT)
#define EMU_FIRST_VISIBLE_ROW 49
#define EMU_LAST_VISIBLE_ROW (EMU_FIRST_VISIBLE_ROW + EMU_PANEL_HEIGHT - 1)

#define EMU_EXIT_TIME_LIMIT 1
#define EMU_EXIT_SCHEDULED 2
#define EMU_EXIT_STEP_LIMIT 3
#define EMU_EXIT_INSN_LIMIT 4
#define EMU_EXIT_HALTED 5
#define EMU_EXIT_WATCHDOG 6
#define EMU_EXIT_RESET 7
#define EMU_EXIT_UC_ERROR 8

typedef void (*emu_gpio_write_cb_t)(uint32_t port, uint32_t offset, uint32_t value);
typedef void (*emu_grf_write_cb_t)(uint32_t offset, uint32_t value);
typedef uint64_t (*emu_mmc_read_cb_t)(uint32_t offset, uint32_t size);
typedef void (*emu_mmc_write_cb_t)(uint32_t offset, uint32_t size, uint64_t value);

typedef struct EmuCore0 EmuCore0;
typedef struct EmuPanel EmuPanel;
typedef struct EmuCore1 EmuCore1;
typedef struct EmuDecoder EmuDecoder;
typedef struct EmuMmc EmuMmc;

typedef struct {
    uint8_t cmd;
    uint32_t param_offset;
    uint32_t param_count;
} EmuGroup;

typedef struct {
    int32_t c0;
    int32_t c1;
    int32_t r0;
    int32_t r1;
    uint64_t cycles;
    int64_t pixels;
    int64_t nonzero;
} EmuWindow;

typedef struct {
    uint64_t cycles;
    int32_t windows;
    int64_t nonzero;
    int32_t groups;
} EmuSweep;

struct EmuPanel {
    EmuCore0 *core0;
    uint16_t frame[EMU_PANEL_PIXELS];
    uint8_t painted[EMU_PANEL_PIXELS];
    uint64_t pixel_total;
    uint64_t out_of_range;
    int32_t col0;
    int32_t col1;
    int32_t row0;
    int32_t row1;
    int32_t active_window;
    int64_t position;
    int32_t sweep_windows;
    int64_t sweep_nonzero;
    int32_t sweep_next_row;
    uint32_t lit_sweep_count;
    uint32_t vop_regs[1024];
    uint8_t pixel_mode;
    uint32_t dmac_regs[1024];
    uint64_t dmac_pushed_pixels;
    EmuGroup *groups;
    uint32_t group_count;
    uint32_t group_cap;
    uint8_t *params;
    uint32_t param_len;
    uint32_t param_cap;
    EmuWindow *windows;
    uint32_t window_count;
    uint32_t window_cap;
    EmuSweep *sweeps;
    uint32_t sweep_count;
    uint32_t sweep_cap;
};

typedef struct {
    uint64_t base_cycles;
    uint64_t jump_cycles;
    uint64_t core_hz;
    uint64_t pclk_hz;
    uint64_t rate_mark_cycles;
    uint64_t rate_mark_us;
    uint64_t systick_base_cycle;
    uint64_t systick_delivered_wraps;
    uint32_t systick_reload;
    uint32_t systick_vtor;
    uint8_t systick_enabled;
    uint8_t systick_tick_interrupt;
    uint8_t exception_depth;
    uint8_t pending_return;
    uint8_t halting;
    uint8_t budget_hit;
    uint8_t rest_pending;
    uint8_t python_exit;
    uint8_t reset_requested_flag;
    uint8_t card_present;
    uint8_t balanced_plugged;
    uint8_t vbus;
    uint8_t cable_bit_dead;
    uint8_t wdog_running;
    uint8_t pad0[2];
    uint32_t wdog_top;
    uint32_t adc_key_value;
    uint32_t adc_battery_value;
    uint32_t parked;
    uint32_t interrupted_parked;
    uint32_t entered;
    uint32_t resumed;
    uint32_t last_err;
    uint32_t has_sched;
    uint64_t wdog_last_kick;
    uint64_t wdog_kicks;
    uint64_t reset_cycle;
    int64_t power_key_pressed_at_ms;
    int64_t power_key_released_at_ms;
    int64_t next_sched_ms;
    uint64_t pending_insns;
    uint64_t slice_insns;
} EmuCore0Header;

typedef struct {
    EmuCore0 *core0;
    uint32_t port;
} EmuGpioCtx;

struct EmuCore0 {
    EmuCore0Header h;
    uc_engine *uc;
    uint8_t *ram_buf;
    uint8_t *core1_ram_buf;
    EmuPanel *panel;
    uc_hook block_hook;
    uint64_t budget_cycle;
    int64_t run_until_ms;
    int run_has_until_ms;
    uint64_t run_step_limit;
    uint64_t run_max_insns;
    uint64_t run_start_cycles;
    int in_run;
    uint32_t systick_regs[1024];
    uint32_t adc_regs[1024];
    uint32_t wdog_regs[1024];
    uint32_t gpio_regs[3][1024];
    uint8_t gpio_seen[3][2];
    EmuGpioCtx gpio_ctx[3];
    uint32_t grf_regs[1024];
    emu_gpio_write_cb_t gpio_write_cb;
    emu_grf_write_cb_t grf_write_cb;
};

typedef struct {
    uint32_t base;
    uint32_t dirty;
    uint32_t regs[1024];
    uint32_t touched[32];
} EmuGenericPage;

struct EmuCore1 {
    uc_engine *uc;
    uint8_t *code_buf;
    uint8_t *priv_ram_buf;
    uint8_t *shared_ram_buf;
    uint8_t *scb_buf;
    uint8_t *periph_buf;
    uint8_t is_shared;
    uint64_t instructions;
    uint64_t slice_insns;
    uint64_t slice_budget;
    uint32_t pc;
    uint32_t clock;
    uint32_t *clock_writes;
    uint32_t clock_write_count;
    uint32_t clock_write_cap;
    uc_hook block_hook;
    uc_hook clock_hook;
    EmuDecoder *decoder;
    uint32_t block_cache[EMU_CORE1_CODE_CACHE_LEN];
};

typedef struct {
    uint64_t instructions;
    uint64_t wakes;
    double busy_us;
    double time_us;
    double period_start;
    double next_wake_us;
    uint32_t mhz;
    uint32_t pll_mhz;
    uint32_t divider;
    uint32_t reload;
    uint32_t wfi;
    uint32_t counter;
    uint32_t ring_bytes;
    uint8_t in_units;
    uint8_t resting;
    uint8_t awake;
    uint8_t has_copied;
    uint32_t copied_gen;
    uint32_t copied_written;
    uint32_t clock_count;
    uint32_t clock_mhz[16];
    double clock_us[16];
} EmuDecoderHeader;

struct EmuDecoder {
    EmuDecoderHeader h;
    EmuCore1 *core1;
    uc_engine *host_uc;
    uint8_t *host_core1_ram;
    uint64_t stored_mask[2];
    double limit_us;
    uint64_t run_insns;
    uint8_t use_limit;
    uc_hook wfi_hook;
    uc_hook read_cur_hook;
    uc_hook write_sys_hook;
    uc_hook write_cru_hook;
    uc_hook store_hook;
};

typedef void (*EmuSnapshotRegion)(void *data, size_t size, size_t capacity);

#define SNAP_FIELD(obj, member) visit(&(obj)->member, sizeof((obj)->member), sizeof((obj)->member))
#define SNAP_RANGE(type, obj, first, end)                                                          \
    do {                                                                                           \
        size_t size = offsetof(type, end) - offsetof(type, first);                                 \
        visit(&(obj)->first, size, size);                                                          \
    } while (0)
#define SNAP_BUFFER(obj, member, count, cap)                                                       \
    visit((obj)->member, (obj)->count * sizeof(*(obj)->member), (obj)->cap * sizeof(*(obj)->member))

void emu_core0_snapshot(EmuCore0 *c, EmuSnapshotRegion visit)
{
    EmuPanel *p = c->panel;
    SNAP_FIELD(c, h);
    SNAP_RANGE(EmuCore0, c, budget_cycle, gpio_ctx);
    SNAP_FIELD(c, grf_regs);
    SNAP_RANGE(EmuPanel, p, frame, groups);
    SNAP_FIELD(p, group_count);
    SNAP_FIELD(p, param_len);
    SNAP_FIELD(p, window_count);
    SNAP_FIELD(p, sweep_count);
    SNAP_BUFFER(p, groups, group_count, group_cap);
    SNAP_BUFFER(p, params, param_len, param_cap);
    SNAP_BUFFER(p, windows, window_count, window_cap);
    SNAP_BUFFER(p, sweeps, sweep_count, sweep_cap);
}

void emu_core1_snapshot(EmuCore1 *c, EmuSnapshotRegion visit)
{
    SNAP_RANGE(EmuCore1, c, instructions, clock_writes);
    SNAP_FIELD(c, clock_write_count);
    SNAP_BUFFER(c, clock_writes, clock_write_count, clock_write_cap);
    SNAP_FIELD(c, block_cache);
}

void emu_decoder_snapshot(EmuDecoder *d, EmuSnapshotRegion visit)
{
    SNAP_FIELD(d, h);
    SNAP_RANGE(EmuDecoder, d, stored_mask, wfi_hook);
}

void emu_generic_page_snapshot(EmuGenericPage *p, EmuSnapshotRegion visit)
{
    visit(p, sizeof(*p), sizeof(*p));
}

typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t permissions;
    void *data;
    uc_cb_mmio_read_t read;
    uc_cb_mmio_write_t write;
} EmuMapping;

static size_t map_regions(uc_engine *uc, const EmuMapping *regions, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const EmuMapping *r = &regions[i];
        uc_err err = r->read
                         ? uc_mmio_map(uc, r->base, r->size, r->read, r->data, r->write, r->data)
                         : uc_mem_map_ptr(uc, r->base, r->size, r->permissions, r->data);
        if (err != UC_ERR_OK) {
            return i;
        }
    }
    return count;
}

static void unmap_regions(uc_engine *uc, const EmuMapping *regions, size_t count)
{
    while (count > 0u) {
        const EmuMapping *r = &regions[--count];
        uc_mem_unmap(uc, r->base, r->size);
    }
}

static void *alloc_page_aligned(size_t size)
{
    void *ptr = NULL;
    if (posix_memalign(&ptr, 4096u, size) != 0 || ptr == NULL) {
        return NULL;
    }
    memset(ptr, 0, size);
    return ptr;
}

static inline uint32_t count_thumb_insns(const uint8_t *code, uint32_t size)
{
    uint32_t count = 0;
    uint32_t offset = 0;
    while (offset + 1u < size) {
        uint16_t hw = (uint16_t)((uint16_t)code[offset] | ((uint16_t)code[offset + 1u] << 8));
        uint16_t top = hw & 0xF800u;
        offset += (top == 0xE800u || top == 0xF000u || top == 0xF800u) ? 4u : 2u;
        count++;
    }
    return count > 0 ? count : 1u;
}

static inline uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline uint32_t subword_mask(unsigned size)
{
    if (size >= 4u) {
        return 0xFFFFFFFFu;
    }
    return (1u << (size * 8u)) - 1u;
}

static inline uint32_t merge_subword(uint32_t old_val, uint64_t offset, unsigned size,
                                     uint64_t value)
{
    if (size >= 4u) {
        return (uint32_t)(value & 0xFFFFFFFFu);
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    uint32_t mask = subword_mask(size) << shift;
    return (old_val & ~mask) | (((uint32_t)value << shift) & mask);
}

static inline uint64_t core0_cycles(const EmuCore0 *c)
{
    return c->h.base_cycles + c->h.jump_cycles;
}

static inline int64_t core0_microseconds(const EmuCore0 *c)
{
    uint64_t since = core0_cycles(c) - c->h.rate_mark_cycles;
    uint64_t hz = c->h.core_hz ? c->h.core_hz : 24000000ULL;
    return (int64_t)(c->h.rate_mark_us + (since * 1000000ULL) / hz);
}

static inline int64_t core0_milliseconds(const EmuCore0 *c)
{
    return core0_microseconds(c) / 1000;
}

static inline uint64_t systick_elapsed(const EmuCore0 *c)
{
    return core0_cycles(c) - c->h.systick_base_cycle;
}

static inline uint64_t systick_wraps(const EmuCore0 *c)
{
    if (!c->h.systick_enabled || c->h.systick_reload == 0u) {
        return 0ULL;
    }
    uint64_t period = (uint64_t)c->h.systick_reload + 1ULL;
    return (systick_elapsed(c) + 1ULL) / period;
}

static inline int systick_pending(const EmuCore0 *c)
{
    return systick_wraps(c) > c->h.systick_delivered_wraps;
}

static inline uint32_t systick_current(const EmuCore0 *c)
{
    if (!c->h.systick_enabled) {
        return 0u;
    }
    uint64_t period = (uint64_t)c->h.systick_reload + 1ULL;
    return (uint32_t)((uint64_t)c->h.systick_reload - (systick_elapsed(c) % period));
}

static inline void systick_acknowledge(EmuCore0 *c)
{
    c->h.systick_delivered_wraps = systick_wraps(c);
}

static inline void systick_restart(EmuCore0 *c)
{
    c->h.systick_base_cycle = core0_cycles(c);
    c->h.systick_delivered_wraps = 0ULL;
}

static inline void core0_poll_jump(EmuCore0 *c, uint64_t delta)
{
    if (!(c->h.systick_tick_interrupt && c->h.systick_enabled && systick_pending(c))) {
        c->h.jump_cycles += delta;
    }
}

static inline uint64_t wdog_timeout_cycles(const EmuCore0 *c)
{
    uint64_t clocks = 1ULL << (16u + c->h.wdog_top);
    uint64_t pclk = c->h.pclk_hz ? c->h.pclk_hz : 24000000ULL;
    return (clocks * c->h.core_hz) / pclk;
}

static inline int wdog_expired(const EmuCore0 *c)
{
    if (!c->h.wdog_running) {
        return 0;
    }
    return (core0_cycles(c) - c->h.wdog_last_kick) > wdog_timeout_cycles(c);
}

static uint64_t cycle_for_ms(const EmuCore0 *c, int64_t target_ms)
{
    int64_t target_us = target_ms * 1000;
    if (target_us <= (int64_t)c->h.rate_mark_us) {
        return c->h.rate_mark_cycles;
    }
    uint64_t need_us = (uint64_t)(target_us - (int64_t)c->h.rate_mark_us);
    uint64_t hz = c->h.core_hz ? c->h.core_hz : 24000000ULL;
    uint64_t need_cycles = (need_us * hz + 999999ULL) / 1000000ULL;
    return c->h.rate_mark_cycles + need_cycles;
}

static void core0_recompute_budget(EmuCore0 *c)
{
    uint64_t cur = core0_cycles(c);
    uint64_t target = cur + EMU_MAX_RUN_BUDGET;
    if (c->h.systick_enabled && c->h.systick_tick_interrupt && c->h.systick_reload > 0u &&
        c->h.exception_depth == 0u) {
        if (systick_pending(c)) {
            uint64_t masked = cur + EMU_MASKED_SLICE_INSTRUCTIONS;
            if (masked < target) {
                target = masked;
            }
        } else {
            uint64_t period = (uint64_t)c->h.systick_reload + 1ULL;
            uint64_t next_wrap =
                c->h.systick_base_cycle + (c->h.systick_delivered_wraps + 1ULL) * period - 1ULL;
            if (next_wrap < target) {
                target = next_wrap;
            }
        }
    }
    if (c->run_has_until_ms) {
        uint64_t until_cyc = cycle_for_ms(c, c->run_until_ms);
        if (until_cyc < target) {
            target = until_cyc;
        }
    }
    if (c->h.has_sched) {
        uint64_t sched_cyc = cycle_for_ms(c, c->h.next_sched_ms);
        if (sched_cyc < target) {
            target = sched_cyc;
        }
    }
    if (c->run_step_limit > 0ULL) {
        uint64_t step_cyc = c->run_start_cycles + c->run_step_limit;
        if (step_cyc < target) {
            target = step_cyc;
        }
    }
    if (c->run_max_insns > 0ULL) {
        uint64_t rem =
            (c->run_max_insns > c->h.slice_insns) ? (c->run_max_insns - c->h.slice_insns) : 1ULL;
        uint64_t max_cyc = cur + rem;
        if (max_cyc < target) {
            target = max_cyc;
        }
    }
    if (c->h.wdog_running) {
        uint64_t expiry = c->h.wdog_last_kick + wdog_timeout_cycles(c) + 1ULL;
        if (expiry < target) {
            target = expiry;
        }
    }
    if (target <= cur) {
        target = cur + 1ULL;
    }
    c->budget_cycle = target;
}

static void panel_track_sweep(EmuPanel *p, const EmuWindow *w)
{
    int full_width = (w->c0 == 0 && w->c1 == EMU_PANEL_WIDTH - 1);
    if (full_width && w->r0 == p->sweep_next_row) {
        p->sweep_windows += 1;
        p->sweep_nonzero += w->nonzero;
        p->sweep_next_row = w->r1 + 1;
    } else if (full_width && w->r0 == EMU_FIRST_VISIBLE_ROW) {
        p->sweep_windows = 1;
        p->sweep_nonzero = w->nonzero;
        p->sweep_next_row = w->r1 + 1;
    } else {
        p->sweep_windows = 0;
        p->sweep_next_row = EMU_FIRST_VISIBLE_ROW;
        return;
    }
    if (p->sweep_next_row == EMU_LAST_VISIBLE_ROW + 1) {
        if (p->sweep_count == p->sweep_cap) {
            uint32_t next_cap = p->sweep_cap ? p->sweep_cap * 2u : 32u;
            EmuSweep *buf = (EmuSweep *)realloc(p->sweeps, next_cap * sizeof(EmuSweep));
            if (buf != NULL) {
                p->sweeps = buf;
                p->sweep_cap = next_cap;
            }
        }
        if (p->sweep_count < p->sweep_cap) {
            EmuSweep *s = &p->sweeps[p->sweep_count++];
            s->cycles = p->core0 ? core0_cycles(p->core0) : 0ULL;
            s->windows = p->sweep_windows;
            s->nonzero = p->sweep_nonzero;
            s->groups = (int32_t)p->group_count;
            if (s->nonzero > 0) {
                p->lit_sweep_count++;
            }
        }
        p->sweep_windows = 0;
        p->sweep_nonzero = 0;
        p->sweep_next_row = EMU_FIRST_VISIBLE_ROW;
    }
}

void emu_panel_command(EmuPanel *p, uint8_t value)
{
    p->active_window = -1;
    if (p->group_count == p->group_cap) {
        uint32_t next_cap = p->group_cap ? p->group_cap * 2u : 256u;
        EmuGroup *buf = (EmuGroup *)realloc(p->groups, next_cap * sizeof(EmuGroup));
        if (buf != NULL) {
            p->groups = buf;
            p->group_cap = next_cap;
        }
    }
    if (p->group_count < p->group_cap) {
        EmuGroup *g = &p->groups[p->group_count++];
        g->cmd = value;
        g->param_offset = p->param_len;
        g->param_count = 0u;
    }
    if (value == EMU_RAMWR) {
        if (p->window_count == p->window_cap) {
            uint32_t next_cap = p->window_cap ? p->window_cap * 2u : 128u;
            EmuWindow *buf = (EmuWindow *)realloc(p->windows, next_cap * sizeof(EmuWindow));
            if (buf != NULL) {
                p->windows = buf;
                p->window_cap = next_cap;
            }
        }
        if (p->window_count < p->window_cap) {
            EmuWindow *w = &p->windows[p->window_count];
            w->c0 = p->col0;
            w->c1 = p->col1;
            w->r0 = p->row0;
            w->r1 = p->row1;
            w->cycles = p->core0 ? core0_cycles(p->core0) : 0ULL;
            w->pixels = 0;
            w->nonzero = 0;
            p->active_window = (int32_t)p->window_count;
            p->window_count++;
            p->position = 0;
        }
    }
}

void emu_panel_parameter(EmuPanel *p, uint8_t value)
{
    if (p->group_count == 0u) {
        return;
    }
    if (p->param_len == p->param_cap) {
        uint32_t next_cap = p->param_cap ? p->param_cap * 2u : 1024u;
        uint8_t *buf = (uint8_t *)realloc(p->params, next_cap);
        if (buf != NULL) {
            p->params = buf;
            p->param_cap = next_cap;
        }
    }
    if (p->param_len >= p->param_cap) {
        return;
    }
    p->params[p->param_len++] = value;
    EmuGroup *g = &p->groups[p->group_count - 1u];
    g->param_count++;
    if ((g->cmd == EMU_CASET || g->cmd == EMU_RASET) && g->param_count == 4u) {
        const uint8_t *pr = p->params + g->param_offset;
        int32_t a = ((int32_t)pr[0] << 8) | (int32_t)pr[1];
        int32_t b = ((int32_t)pr[2] << 8) | (int32_t)pr[3];
        if (g->cmd == EMU_CASET) {
            p->col0 = a;
            p->col1 = b;
        } else {
            p->row0 = a;
            p->row1 = b;
        }
    }
}

void emu_panel_pixel(EmuPanel *p, uint16_t value)
{
    if (p->active_window < 0) {
        return;
    }
    EmuWindow *w = &p->windows[p->active_window];
    int32_t width = w->c1 - w->c0 + 1;
    int32_t height = w->r1 - w->r0 + 1;
    w->pixels++;
    if (value != 0u) {
        w->nonzero++;
    }
    p->pixel_total++;
    if (width > 0) {
        int64_t x = w->c0 + (p->position % width);
        int64_t y = w->r0 + (p->position / width) - EMU_FIRST_VISIBLE_ROW;
        p->position++;
        if (x >= 0 && x < EMU_PANEL_WIDTH && y >= 0 && y < EMU_PANEL_HEIGHT) {
            int32_t idx = y * EMU_PANEL_WIDTH + x;
            p->frame[idx] = value;
            p->painted[idx] = 1u;
        } else {
            p->out_of_range++;
        }
    } else {
        p->position++;
        p->out_of_range++;
    }
    if (w->pixels == (int64_t)width * (int64_t)height) {
        p->active_window = -1;
        panel_track_sweep(p, w);
    }
}

void emu_panel_pixels(EmuPanel *p, const uint16_t *values, uint32_t len)
{
    if (p->active_window < 0 || len == 0u) {
        return;
    }
    EmuWindow *w = &p->windows[p->active_window];
    int32_t width = w->c1 - w->c0 + 1;
    int32_t height = w->r1 - w->r0 + 1;
    if (width <= 0 || height <= 0) {
        for (uint32_t i = 0; i < len; i++) {
            emu_panel_pixel(p, values[i]);
        }
        return;
    }
    int64_t remain = (int64_t)width * (int64_t)height - w->pixels;
    if (remain <= 0) {
        return;
    }
    uint32_t count = ((int64_t)len < remain) ? len : (uint32_t)remain;
    uint32_t nonzero = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (values[i] != 0u) {
            nonzero++;
        }
    }
    w->pixels += count;
    w->nonzero += nonzero;
    p->pixel_total += count;
    uint32_t done = 0;
    while (done < count) {
        int64_t row = p->position / width;
        int32_t col = (int32_t)(p->position % width);
        uint32_t span = (uint32_t)(width - col);
        if (count - done < span) {
            span = count - done;
        }
        int64_t y = w->r0 + row - EMU_FIRST_VISIBLE_ROW;
        int32_t x = w->c0 + col;
        int32_t first = x > 0 ? x : 0;
        int32_t last =
            (x + (int32_t)span < EMU_PANEL_WIDTH) ? (x + (int32_t)span) : EMU_PANEL_WIDTH;
        if (y < 0 || y >= EMU_PANEL_HEIGHT || first >= last) {
            p->out_of_range += span;
        } else {
            int32_t dst_start = y * EMU_PANEL_WIDTH + first;
            int32_t copy_len = last - first;
            memcpy(&p->frame[dst_start], &values[done + (uint32_t)(first - x)],
                   (size_t)copy_len * sizeof(uint16_t));
            memset(&p->painted[dst_start], 1, (size_t)copy_len);
            p->out_of_range += (uint64_t)(span - (uint32_t)copy_len);
        }
        p->position += (int32_t)span;
        done += span;
    }
    if (w->pixels == (int64_t)width * (int64_t)height) {
        p->active_window = -1;
        panel_track_sweep(p, w);
    }
}

static uint64_t vop_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuPanel *p = (EmuPanel *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = 0u;
    if (base == EMU_VOP_STATUS) {
        word = 0u;
    } else if ((base >> 2) < 1024u) {
        word = p->vop_regs[base >> 2];
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void vop_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuPanel *p = (EmuPanel *)ud;
    if (offset == EMU_VOP_DATA && p->pixel_mode) {
        emu_panel_pixel(p, (uint16_t)(value & 0xFFFFu));
        return;
    }
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    uint32_t merged = merge_subword(p->vop_regs[idx], offset, size, value);
    p->vop_regs[idx] = merged;
    if (base == EMU_VOP_CONTROL) {
        p->pixel_mode = (merged == EMU_VOP_CONTROL_PIXELS) ? 1u : 0u;
    } else if (base == EMU_VOP_COMMAND) {
        emu_panel_command(p, (uint8_t)(merged & 0xFFu));
    } else if (base == EMU_VOP_DATA) {
        emu_panel_parameter(p, (uint8_t)(merged & 0xFFu));
    }
}

static void dmac_push_block(EmuPanel *p, uint32_t source, uint32_t words)
{
    if (words == 0u || words > UINT32_MAX / 4u) {
        return;
    }
    uint32_t pixels = words * 2u;
    uint32_t byte_len = words * 4u;
    EmuCore0 *c = p->core0;
    if (c != NULL && source % _Alignof(uint16_t) == 0u && source >= EMU_RAM_BASE &&
        (uint64_t)source + (uint64_t)byte_len <= (uint64_t)EMU_RAM_BASE + EMU_RAM_SIZE) {
        const uint16_t *src = (const uint16_t *)(c->ram_buf + (source - EMU_RAM_BASE));
        emu_panel_pixels(p, src, pixels);
        p->dmac_pushed_pixels += pixels;
        return;
    }
    if (c != NULL && c->uc != NULL) {
        uint16_t *tmp = (uint16_t *)malloc(byte_len);
        if (tmp != NULL) {
            if (uc_mem_read(c->uc, source, tmp, byte_len) == UC_ERR_OK) {
                emu_panel_pixels(p, tmp, pixels);
                p->dmac_pushed_pixels += pixels;
            }
            free(tmp);
        }
    }
}

static void dmac_push(EmuPanel *p)
{
    uint32_t item = p->dmac_regs[0x10u >> 2];
    if (item == 0u) {
        dmac_push_block(p, p->dmac_regs[0x00u >> 2], p->dmac_regs[0x1Cu >> 2]);
        return;
    }
    EmuCore0 *c = p->core0;
    while (item != 0u) {
        uint32_t desc[5];
        if (c != NULL && item >= EMU_RAM_BASE &&
            (uint64_t)item + 20ULL <= (uint64_t)EMU_RAM_BASE + EMU_RAM_SIZE) {
            memcpy(desc, c->ram_buf + (item - EMU_RAM_BASE), 20u);
        } else if (c != NULL && c->uc != NULL) {
            if (uc_mem_read(c->uc, item, desc, 20u) != UC_ERR_OK) {
                break;
            }
        } else {
            break;
        }
        uint32_t source = desc[0];
        item = desc[2];
        uint32_t words = desc[4];
        dmac_push_block(p, source, words);
    }
}

static uint64_t dmac_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuPanel *p = (EmuPanel *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = 0u;
    if (base == EMU_DMAC_COMP_TYPE) {
        word = 0x44571110u;
    } else if ((base >> 2) < 1024u) {
        word = p->dmac_regs[base >> 2];
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void dmac_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuPanel *p = (EmuPanel *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    uint32_t merged = merge_subword(p->dmac_regs[idx], offset, size, value);
    p->dmac_regs[idx] = merged;
    if (base == EMU_DMAC_CHANNEL_ENABLE && (merged & (EMU_DMAC_PUSH_BIT << 8)) &&
        (merged & EMU_DMAC_PUSH_BIT)) {
        p->dmac_regs[idx] = merged & ~EMU_DMAC_PUSH_BIT;
        dmac_push(p);
    }
}

static uint64_t systick_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = 0u;
    if (base == EMU_SYSTICK_CURRENT) {
        core0_poll_jump(c, EMU_SYSTICK_POLL_JUMP_CYCLES);
        word = systick_current(c);
    } else if (base == EMU_SCB_ICSR) {
        core0_poll_jump(c, EMU_SYSTICK_POLL_JUMP_CYCLES);
        word = systick_pending(c) ? EMU_ICSR_PENDSTSET : 0u;
    } else if (base == EMU_SYSTICK_CTRL) {
        word = (c->h.systick_enabled ? 1u : 0u) | (c->h.systick_tick_interrupt ? 2u : 0u);
    } else if (base == EMU_SCB_VTOR) {
        word = c->h.systick_vtor;
    } else if ((base >> 2) < 1024u) {
        word = c->systick_regs[base >> 2];
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void systick_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value,
                               void *ud)
{
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    uint32_t merged = merge_subword(c->systick_regs[idx], offset, size, value);
    if (base == EMU_SYSTICK_CTRL) {
        uint8_t was = c->h.systick_enabled;
        c->h.systick_enabled = (merged & 1u) ? 1u : 0u;
        c->h.systick_tick_interrupt = (merged & EMU_SYSTICK_TICKINT) ? 1u : 0u;
        if (c->h.systick_enabled && !was) {
            systick_restart(c);
        }
        if (c->in_run) {
            core0_recompute_budget(c);
        }
    } else if (base == EMU_SYSTICK_RELOAD) {
        c->h.systick_reload = merged & 0xFFFFFFu;
        systick_restart(c);
        if (c->in_run) {
            core0_recompute_budget(c);
        }
    } else if (base == EMU_SYSTICK_CURRENT) {
        systick_restart(c);
        if (c->in_run) {
            core0_recompute_budget(c);
        }
    } else if (base == EMU_SCB_ICSR) {
        if (merged & EMU_ICSR_PENDSTCLR) {
            systick_acknowledge(c);
        }
    } else if (base == EMU_SCB_VTOR) {
        c->h.systick_vtor = merged;
    } else if (base == EMU_SCB_AIRCR) {
        if ((merged & 0xFFFF0000u) == EMU_AIRCR_VECTKEY && (merged & EMU_AIRCR_SYSRESETREQ)) {
            c->h.reset_requested_flag = 1u;
            c->h.reset_cycle = core0_cycles(c);
            c->h.halting = 1u;
            uc_emu_stop(uc);
        }
    } else {
        c->systick_regs[idx] = merged;
    }
}

static uint64_t adc_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = 0u;
    if (base == EMU_ADC_RESULT) {
        uint32_t channel = c->adc_regs[EMU_ADC_CONTROL >> 2] & 7u;
        word = (channel == EMU_ADC_KEY_CHANNEL) ? c->h.adc_key_value : c->h.adc_battery_value;
    } else if ((base >> 2) < 1024u) {
        word = c->adc_regs[base >> 2];
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void adc_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    c->adc_regs[idx] = merge_subword(c->adc_regs[idx], offset, size, value);
}

static uint64_t wdog_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = ((base >> 2) < 1024u) ? c->wdog_regs[base >> 2] : 0u;
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void wdog_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    uint32_t merged = merge_subword(c->wdog_regs[idx], offset, size, value);
    c->wdog_regs[idx] = merged;
    if (base == 0x0Cu && merged == 0x76u) {
        c->h.wdog_kicks++;
        c->h.wdog_last_kick = core0_cycles(c);
    } else if (base == 0x04u) {
        c->h.wdog_top = merged & 0xFu;
        if (c->in_run) {
            core0_recompute_budget(c);
        }
    } else if (base == 0x00u) {
        c->h.wdog_running = (merged & 1u) ? 1u : 0u;
        if (c->in_run) {
            core0_recompute_budget(c);
        }
    }
}

static uint32_t external_pin_levels(const EmuCore0 *c, uint32_t port)
{
    uint32_t levels = 0u;
    if (port == EMU_CHARGE_PORT) {
        levels |= (1u << EMU_CHARGE_PIN);
        int64_t ms = core0_milliseconds(c);
        if (c->h.power_key_pressed_at_ms <= ms && ms < c->h.power_key_released_at_ms) {
            levels |= (1u << EMU_POWER_KEY_PIN);
        }
    }
    if (port == EMU_CARD_DETECT_PORT && !c->h.card_present) {
        levels |= (1u << EMU_CARD_DETECT_PIN);
    }
    if (port == EMU_BALANCED_PORT && c->h.balanced_plugged) {
        levels |= (1u << EMU_BALANCED_PIN);
    }
    return levels;
}

static uint64_t gpio_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuGpioCtx *ctx = (EmuGpioCtx *)ud;
    EmuCore0 *c = ctx->core0;
    uint32_t port = ctx->port;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = 0u;
    if (base == EMU_GPIO_INPUT_LEVEL) {
        uint32_t data = c->gpio_regs[port][EMU_GPIO_DATA >> 2];
        uint32_t dir = c->gpio_regs[port][EMU_GPIO_DIRECTION >> 2];
        uint32_t ext = external_pin_levels(c, port);
        word = (data & dir) | (ext & ~dir);
    } else if ((base >> 2) < 1024u) {
        word = c->gpio_regs[port][base >> 2];
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void gpio_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuGpioCtx *ctx = (EmuGpioCtx *)ud;
    EmuCore0 *c = ctx->core0;
    uint32_t port = ctx->port;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    uint32_t before = c->gpio_regs[port][idx];
    uint32_t merged = merge_subword(before, offset, size, value);
    c->gpio_regs[port][idx] = merged;
    if (base == EMU_GPIO_DATA || base == EMU_GPIO_DIRECTION) {
        uint32_t slot = (base == EMU_GPIO_DATA) ? 0u : 1u;
        if (!c->gpio_seen[port][slot] || before != merged) {
            c->gpio_seen[port][slot] = 1u;
            if (c->gpio_write_cb != NULL) {
                c->gpio_write_cb(port, base, merged);
            }
        }
    }
}

static uint64_t grf_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t word = 0u;
    if (base == EMU_GRF_VBUS) {
        word = (c->h.vbus && !c->h.cable_bit_dead) ? EMU_GRF_VBUS_PRESENT : 0u;
    } else if ((base >> 2) < 1024u) {
        word = c->grf_regs[base >> 2];
    }
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void grf_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuCore0 *c = (EmuCore0 *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    uint32_t merged = merge_subword(c->grf_regs[idx], offset, size, value);
    uint32_t mask = merged >> 16;
    uint32_t old = c->grf_regs[idx];
    c->grf_regs[idx] = (old & ~mask & 0xFFFFu) | (merged & mask & 0xFFFFu);
    if (c->grf_write_cb != NULL) {
        c->grf_write_cb(base, merged);
    }
}

static uint64_t generic_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuGenericPage *gp = (EmuGenericPage *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return 0u;
    }
    gp->touched[idx >> 5] |= (1u << (idx & 31u));
    gp->dirty = 1u;
    uint32_t word = gp->regs[idx];
    unsigned shift = (unsigned)(offset & 3u) * 8u;
    return (word >> shift) & subword_mask(size);
}

static void generic_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value,
                               void *ud)
{
    (void)uc;
    EmuGenericPage *gp = (EmuGenericPage *)ud;
    uint32_t base = (uint32_t)(offset & ~3ULL);
    uint32_t idx = base >> 2;
    if (idx >= 1024u) {
        return;
    }
    gp->touched[idx >> 5] |= (1u << (idx & 31u));
    gp->dirty = 1u;
    gp->regs[idx] = merge_subword(gp->regs[idx], offset, size, value);
}

struct EmuMmc {
    EmuCore0 *core0;
    uint64_t busy_until;
    uint32_t rintsts;
    uint32_t read_error_after;
    uint32_t read_error_status;
    uint32_t read_error_pending;
    uint32_t *fifo;
    uint32_t fifo_head;
    uint32_t fifo_len;
    uint32_t fifo_cap;
    emu_mmc_read_cb_t slow_read_cb;
    emu_mmc_write_cb_t write_cb;
};

void emu_mmc_snapshot(EmuMmc *m, EmuSnapshotRegion visit)
{
    SNAP_RANGE(EmuMmc, m, busy_until, fifo);
    SNAP_FIELD(m, fifo_head);
    SNAP_FIELD(m, fifo_len);
    SNAP_BUFFER(m, fifo, fifo_len, fifo_cap);
}

static inline int mmc_busy(EmuMmc *m)
{
    uint64_t cur = core0_cycles(m->core0);
    if (m->busy_until > cur) {
        uint64_t rem = m->busy_until - cur;
        uint64_t step = rem < 240ULL ? rem : 240ULL;
        core0_poll_jump(m->core0, step);
    }
    return m->busy_until > core0_cycles(m->core0);
}

static uint64_t mmc_mmio_read(uc_engine *uc, uint64_t offset, unsigned size, void *ud)
{
    (void)uc;
    EmuMmc *m = (EmuMmc *)ud;
    if (offset == 0x200ULL) {
        if (m->fifo_head < m->fifo_len && !mmc_busy(m)) {
            if (m->read_error_after != 0u && --m->read_error_after == 0u) {
                m->read_error_pending |= m->read_error_status;
            }
            return m->fifo[m->fifo_head++];
        }
        return 0ULL;
    }
    uint32_t base = (uint32_t)(offset & ~3ULL);
    if (base == 0x48u) {
        int waiting = mmc_busy(m);
        uint32_t avail = m->fifo_len - m->fifo_head;
        uint32_t count = waiting ? 0u : (avail < 0x1FFFu ? avail : 0x1FFFu);
        uint32_t empty = (count == 0u) ? (1u << 2) : 0u;
        uint32_t busy_bit = (waiting && avail == 0u) ? (1u << 9) : 0u;
        uint32_t word = empty | (count << 17) | busy_bit;
        unsigned shift = (unsigned)(offset & 3u) * 8u;
        return (word >> shift) & subword_mask(size);
    }
    if (base == 0x44u) {
        uint32_t word = m->rintsts | m->read_error_pending;
        if (mmc_busy(m)) {
            word &= ~(1u << 3);
        }
        unsigned shift = (unsigned)(offset & 3u) * 8u;
        return (word >> shift) & subword_mask(size);
    }
    if (m->slow_read_cb != NULL) {
        return m->slow_read_cb((uint32_t)offset, (uint32_t)size);
    }
    return 0ULL;
}

static void mmc_mmio_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *ud)
{
    (void)uc;
    EmuMmc *m = (EmuMmc *)ud;
    if ((offset & ~3ULL) == 0x44u) {
        unsigned shift = (unsigned)(offset & 3u) * 8u;
        m->read_error_pending &= ~((uint32_t)(value & subword_mask(size)) << shift);
    }
    if (m->write_cb != NULL) {
        m->write_cb((uint32_t)offset, (uint32_t)size, value);
    }
}

EmuMmc *emu_mmc_map(EmuCore0 *c, uint32_t base, emu_mmc_read_cb_t read_cb,
                    emu_mmc_write_cb_t write_cb)
{
    EmuMmc *m = (EmuMmc *)calloc(1, sizeof(EmuMmc));
    if (m == NULL) {
        return NULL;
    }
    m->core0 = c;
    m->slow_read_cb = read_cb;
    m->write_cb = write_cb;
    if (uc_mmio_map(c->uc, base, 0x1000u, mmc_mmio_read, m, mmc_mmio_write, m) != UC_ERR_OK) {
        free(m);
        return NULL;
    }
    return m;
}

void emu_mmc_destroy(EmuMmc *m)
{
    if (m == NULL) {
        return;
    }
    free(m->fifo);
    free(m);
}

void emu_mmc_sync(EmuMmc *m, uint64_t busy_until, uint32_t rintsts)
{
    m->busy_until = busy_until;
    m->rintsts = rintsts;
}

void emu_mmc_clear_fifo(EmuMmc *m)
{
    m->fifo_head = 0u;
    m->fifo_len = 0u;
}

void emu_mmc_inject_fifo_read_error(EmuMmc *m, uint32_t after_words, uint32_t status)
{
    m->read_error_after = after_words;
    m->read_error_status = status;
    m->read_error_pending = 0u;
}

void emu_mmc_load_fifo(EmuMmc *m, const uint32_t *words, uint32_t count)
{
    if (count > m->fifo_cap) {
        uint32_t next_cap = m->fifo_cap ? m->fifo_cap : 256u;
        while (next_cap < count) {
            next_cap *= 2u;
        }
        uint32_t *buf = (uint32_t *)realloc(m->fifo, (size_t)next_cap * sizeof(uint32_t));
        if (buf == NULL) {
            return;
        }
        m->fifo = buf;
        m->fifo_cap = next_cap;
    }
    if (count > 0u) {
        memcpy(m->fifo, words, (size_t)count * sizeof(uint32_t));
    }
    m->fifo_head = 0u;
    m->fifo_len = count;
}

static void core0_trampoline_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *ud)
{
    (void)addr;
    (void)size;
    EmuCore0 *c = (EmuCore0 *)ud;
    c->h.pending_return = 1u;
    c->h.halting = 1u;
    uc_emu_stop(uc);
}

static void core0_read_mem(EmuCore0 *c, uint32_t addr, void *dst, uint32_t len)
{
    if (addr >= EMU_RAM_BASE &&
        (uint64_t)addr + (uint64_t)len <= (uint64_t)EMU_RAM_BASE + EMU_RAM_SIZE) {
        memcpy(dst, c->ram_buf + (addr - EMU_RAM_BASE), len);
        return;
    }
    uc_mem_read(c->uc, addr, dst, len);
}

static void core0_write_mem(EmuCore0 *c, uint32_t addr, const void *src, uint32_t len)
{
    if (addr >= EMU_RAM_BASE &&
        (uint64_t)addr + (uint64_t)len <= (uint64_t)EMU_RAM_BASE + EMU_RAM_SIZE) {
        memcpy(c->ram_buf + (addr - EMU_RAM_BASE), src, len);
        return;
    }
    uc_mem_write(c->uc, addr, src, len);
}

static void core0_enter_systick(EmuCore0 *c)
{
    uint32_t handler = 0u;
    core0_read_mem(c, c->h.systick_vtor + 4u * EMU_SYSTICK_VECTOR_INDEX, &handler, 4u);
    uint64_t msp = 0, pc = 0, xpsr = 0;
    uint64_t regs[6] = {0, 0, 0, 0, 0, 0};
    int ids[6] = {UC_ARM_REG_R0, UC_ARM_REG_R1,  UC_ARM_REG_R2,
                  UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR};
    for (int i = 0; i < 6; i++) {
        uc_reg_read(c->uc, ids[i], &regs[i]);
    }
    uc_reg_read(c->uc, UC_ARM_REG_MSP, &msp);
    uc_reg_read(c->uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(c->uc, UC_ARM_REG_XPSR, &xpsr);
    uint32_t sp = (uint32_t)msp - 32u;
    uint32_t frame[8];
    for (int i = 0; i < 6; i++) {
        frame[i] = (uint32_t)regs[i];
    }
    frame[6] = (uint32_t)(pc | 1u);
    frame[7] = (uint32_t)xpsr;
    core0_write_mem(c, sp, frame, 32u);
    uint64_t new_msp = sp;
    uint64_t new_lr = EMU_TRAMPOLINE | 1u;
    uint64_t new_pc = handler & ~1u;
    uc_reg_write(c->uc, UC_ARM_REG_MSP, &new_msp);
    uc_reg_write(c->uc, UC_ARM_REG_LR, &new_lr);
    uc_reg_write(c->uc, UC_ARM_REG_PC, &new_pc);
    systick_acknowledge(c);
    c->h.exception_depth = 1u;
    c->h.interrupted_parked = c->h.parked;
    c->h.parked = 0u;
}

static void core0_finish_exception(EmuCore0 *c)
{
    uint64_t msp = 0;
    uc_reg_read(c->uc, UC_ARM_REG_MSP, &msp);
    uint32_t frame[8];
    core0_read_mem(c, (uint32_t)msp, frame, 32u);
    int ids[6] = {UC_ARM_REG_R0, UC_ARM_REG_R1,  UC_ARM_REG_R2,
                  UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR};
    for (int i = 0; i < 6; i++) {
        uint64_t val = frame[i];
        uc_reg_write(c->uc, ids[i], &val);
    }
    uint64_t new_msp = (uint32_t)msp + 32u;
    uint64_t new_xpsr = frame[7];
    uint64_t new_pc = frame[6] & ~1u;
    uc_reg_write(c->uc, UC_ARM_REG_MSP, &new_msp);
    uc_reg_write(c->uc, UC_ARM_REG_XPSR, &new_xpsr);
    uc_reg_write(c->uc, UC_ARM_REG_PC, &new_pc);
    c->h.exception_depth = 0u;
    c->h.pending_return = 0u;
    c->h.parked = c->h.interrupted_parked;
    c->h.interrupted_parked = 0u;
}

EmuCore0 *emu_core0_create(uc_engine *uc, emu_gpio_write_cb_t gpio_cb, emu_grf_write_cb_t grf_cb)
{
    EmuCore0 *c = (EmuCore0 *)calloc(1, sizeof(EmuCore0));
    if (c == NULL) {
        return NULL;
    }
    c->uc = uc;
    c->h.core_hz = 24000000ULL;
    c->h.pclk_hz = 24000000ULL;
    c->h.adc_key_value = 1023u;
    c->h.adc_battery_value = 819u;
    c->h.power_key_pressed_at_ms = 0;
    c->h.power_key_released_at_ms = 300;
    c->gpio_write_cb = gpio_cb;
    c->grf_write_cb = grf_cb;
    c->ram_buf = (uint8_t *)alloc_page_aligned(EMU_RAM_SIZE);
    c->core1_ram_buf = (uint8_t *)alloc_page_aligned(EMU_CORE1_RAM_SIZE);
    c->panel = (EmuPanel *)calloc(1, sizeof(EmuPanel));
    if (c->ram_buf == NULL || c->core1_ram_buf == NULL || c->panel == NULL) {
        free(c->ram_buf);
        free(c->core1_ram_buf);
        free(c->panel);
        free(c);
        return NULL;
    }
    c->panel->core0 = c;
    c->panel->active_window = -1;
    c->panel->sweep_next_row = EMU_FIRST_VISIBLE_ROW;
    uint32_t low_size = EMU_DATA_LOW_END - EMU_RAM_BASE;
    uint32_t mid_size = EMU_CODE_WINDOW_END - EMU_DATA_LOW_END;
    uint32_t high_size = (EMU_RAM_BASE + EMU_RAM_SIZE) - EMU_CODE_WINDOW_END;
    uint32_t rw = UC_PROT_READ | UC_PROT_WRITE;
    for (uint32_t port = 0; port < 3u; port++) {
        c->gpio_ctx[port].core0 = c;
        c->gpio_ctx[port].port = port;
    }
    const EmuMapping regions[] = {
        {EMU_RAM_BASE, low_size, rw, c->ram_buf, NULL, NULL},
        {EMU_DATA_LOW_END, mid_size, UC_PROT_ALL, c->ram_buf + low_size, NULL, NULL},
        {EMU_CODE_WINDOW_END, high_size, rw, c->ram_buf + low_size + mid_size, NULL, NULL},
        {EMU_CORE1_RAM_BASE, EMU_CORE1_RAM_SIZE, rw, c->core1_ram_buf, NULL, NULL},
        {0x60070000u, 0x1000u, 0u, c->panel, vop_mmio_read, vop_mmio_write},
        {0x60000000u, 0x1000u, 0u, c->panel, dmac_mmio_read, dmac_mmio_write},
        {0xE000E000u, 0x1000u, 0u, c, systick_mmio_read, systick_mmio_write},
        {0x400D0000u, 0x1000u, 0u, c, adc_mmio_read, adc_mmio_write},
        {0x40120000u, 0x1000u, 0u, c, wdog_mmio_read, wdog_mmio_write},
        {0x40160000u, 0x1000u, 0u, &c->gpio_ctx[0], gpio_mmio_read, gpio_mmio_write},
        {0x40170000u, 0x1000u, 0u, &c->gpio_ctx[1], gpio_mmio_read, gpio_mmio_write},
        {0x50030000u, 0x1000u, 0u, &c->gpio_ctx[2], gpio_mmio_read, gpio_mmio_write},
        {0x50010000u, 0x1000u, 0u, c, grf_mmio_read, grf_mmio_write},
    };
    size_t count = sizeof(regions) / sizeof(regions[0]);
    size_t mapped = map_regions(uc, regions, count);
    if (mapped != count ||
        uc_hook_add(uc, &c->block_hook, UC_HOOK_BLOCK, (void *)core0_trampoline_cb, c,
                    EMU_TRAMPOLINE, EMU_TRAMPOLINE) != UC_ERR_OK) {
        unmap_regions(uc, regions, mapped);
        free(c->ram_buf);
        free(c->core1_ram_buf);
        free(c->panel);
        free(c);
        return NULL;
    }
    return c;
}

void emu_core0_destroy(EmuCore0 *c)
{
    if (c == NULL) {
        return;
    }
    if (c->panel != NULL) {
        free(c->panel->groups);
        free(c->panel->params);
        free(c->panel->windows);
        free(c->panel->sweeps);
        free(c->panel);
    }
    free(c->ram_buf);
    free(c->core1_ram_buf);
    free(c);
}

EmuCore0Header *emu_core0_header(EmuCore0 *c)
{
    return &c->h;
}

EmuPanel *emu_core0_panel(EmuCore0 *c)
{
    return c->panel;
}

void *emu_core0_core1_ram(EmuCore0 *c)
{
    return c->core1_ram_buf;
}

void *emu_core0_ram(EmuCore0 *c)
{
    return c->ram_buf;
}

int emu_core0_memcmp(const EmuCore0 *c, uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr >= EMU_RAM_BASE &&
        (uint64_t)addr + (uint64_t)len <= (uint64_t)EMU_RAM_BASE + EMU_RAM_SIZE) {
        return memcmp(c->ram_buf + (addr - EMU_RAM_BASE), data, len) == 0 ? 1 : 0;
    }
    return 0;
}

void emu_core0_systick_restart(EmuCore0 *c)
{
    systick_restart(c);
    if (c->in_run) {
        core0_recompute_budget(c);
    }
}

void emu_core0_systick_acknowledge(EmuCore0 *c)
{
    systick_acknowledge(c);
}

void emu_core0_recompute_budget(EmuCore0 *c)
{
    if (c->in_run) {
        core0_recompute_budget(c);
    }
}

void emu_core0_clear_block_cache(EmuCore0 *c)
{
    (void)c;
}

int emu_core0_run(EmuCore0 *c, int64_t until_ms, int has_until_ms, uint64_t step_limit,
                  uint64_t max_insns)
{
    c->run_until_ms = until_ms;
    c->run_has_until_ms = has_until_ms;
    c->run_step_limit = step_limit;
    c->run_max_insns = max_insns;
    c->run_start_cycles = core0_cycles(c);
    c->h.slice_insns = 0ULL;
    c->h.last_err = UC_ERR_OK;
    c->in_run = 1;
    int exit_code = EMU_EXIT_HALTED;
    for (;;) {
        int64_t now_ms = core0_milliseconds(c);
        if (c->h.has_sched && now_ms >= c->h.next_sched_ms) {
            exit_code = EMU_EXIT_SCHEDULED;
            break;
        }
        if (c->h.pending_return) {
            core0_finish_exception(c);
        }
        if (c->h.exception_depth == 0u && c->h.systick_tick_interrupt && c->h.systick_enabled &&
            systick_pending(c)) {
            uint64_t primask = 0;
            uc_reg_read(c->uc, UC_ARM_REG_PRIMASK, &primask);
            if (primask == 0ULL) {
                core0_enter_systick(c);
            }
        }
        now_ms = core0_milliseconds(c);
        if (has_until_ms && now_ms >= until_ms) {
            exit_code = EMU_EXIT_TIME_LIMIT;
            break;
        }
        if (step_limit > 0ULL && (core0_cycles(c) - c->run_start_cycles) >= step_limit) {
            exit_code = EMU_EXIT_STEP_LIMIT;
            break;
        }
        if (max_insns > 0ULL && c->h.slice_insns >= max_insns) {
            exit_code = EMU_EXIT_INSN_LIMIT;
            break;
        }
        uint64_t pc = 0;
        uc_reg_read(c->uc, UC_ARM_REG_PC, &pc);
        uint32_t start = (uint32_t)pc;
        int waiting = (c->h.systick_tick_interrupt && c->h.systick_enabled && systick_pending(c));
        uint64_t count = (waiting && c->h.exception_depth == 0u) ? EMU_MASKED_SLICE_INSTRUCTIONS
                                                                 : EMU_SLICE_INSTRUCTIONS;
        c->h.resumed = (c->h.parked != 0u && c->h.parked == start) ? c->h.parked : 0u;
        c->h.parked = 0u;
        c->h.entered = 0u;
        c->h.halting = 0u;
        c->h.budget_hit = 0u;
        c->h.rest_pending = 0u;
        c->h.python_exit = 0u;
        c->h.pending_insns = 0ULL;
        uc_err err =
            uc_emu_start(c->uc, (uint64_t)(start | 1u), 0xFFFFFFFEULL, 0ULL, (size_t)count);
        if (err != UC_ERR_OK) {
            c->h.last_err = (uint32_t)err;
            exit_code = EMU_EXIT_UC_ERROR;
            break;
        }
        c->h.base_cycles += count;
        c->h.slice_insns += EMU_SLICE_INSTRUCTIONS;
        if (c->h.reset_requested_flag) {
            exit_code = EMU_EXIT_RESET;
            break;
        }
        if (c->h.entered != 0u && !c->h.halting) {
            uint64_t after_pc = 0;
            uc_reg_read(c->uc, UC_ARM_REG_PC, &after_pc);
            if ((uint32_t)after_pc == c->h.entered) {
                c->h.parked = c->h.entered;
            }
        }
        if (wdog_expired(c)) {
            exit_code = EMU_EXIT_WATCHDOG;
            break;
        }
        if (c->h.python_exit || (c->h.halting && !c->h.pending_return)) {
            exit_code = EMU_EXIT_HALTED;
            break;
        }
    }
    c->in_run = 0;
    return exit_code;
}

int emu_core0_step_once(EmuCore0 *c, uint64_t count)
{
    c->run_has_until_ms = 0;
    c->run_step_limit = count;
    c->run_max_insns = 0ULL;
    c->h.slice_insns = 0ULL;
    c->h.last_err = UC_ERR_OK;
    c->in_run = 1;
    uint64_t pc = 0;
    uc_reg_read(c->uc, UC_ARM_REG_PC, &pc);
    uint32_t start = (uint32_t)pc;
    c->h.resumed = (c->h.parked != 0u && c->h.parked == start) ? c->h.parked : 0u;
    c->h.parked = 0u;
    c->h.entered = 0u;
    c->h.halting = 0u;
    c->h.budget_hit = 0u;
    c->h.rest_pending = 0u;
    c->h.python_exit = 0u;
    c->h.pending_insns = 0ULL;
    uc_err err = uc_emu_start(c->uc, (uint64_t)(start | 1u), 0xFFFFFFFEULL, 0ULL, (size_t)count);
    c->in_run = 0;
    if (err != UC_ERR_OK) {
        c->h.last_err = (uint32_t)err;
        return EMU_EXIT_UC_ERROR;
    }
    c->h.base_cycles += count;
    if (c->h.reset_requested_flag) {
        return EMU_EXIT_RESET;
    }
    if (c->h.entered != 0u && !c->h.halting) {
        uint64_t after_pc = 0;
        uc_reg_read(c->uc, UC_ARM_REG_PC, &after_pc);
        if ((uint32_t)after_pc == c->h.entered) {
            c->h.parked = c->h.entered;
        }
    }
    return EMU_EXIT_STEP_LIMIT;
}

EmuGenericPage *emu_generic_page_map(uc_engine *uc, uint32_t base)
{
    EmuGenericPage *gp = (EmuGenericPage *)calloc(1, sizeof(EmuGenericPage));
    if (gp == NULL) {
        return NULL;
    }
    gp->base = base;
    if (uc_mmio_map(uc, base, 0x1000u, generic_mmio_read, gp, generic_mmio_write, gp) !=
        UC_ERR_OK) {
        free(gp);
        return NULL;
    }
    return gp;
}

void emu_generic_page_destroy(EmuGenericPage *gp)
{
    free(gp);
}

uint32_t emu_generic_page_drain_touched(EmuGenericPage *gp, uint32_t *out_addrs, uint32_t max_out)
{
    if (!gp->dirty) {
        return 0u;
    }
    uint32_t count = 0u;
    for (uint32_t w = 0u; w < 32u; w++) {
        uint32_t bits = gp->touched[w];
        if (bits == 0u) {
            continue;
        }
        gp->touched[w] = 0u;
        for (uint32_t b = 0u; b < 32u; b++) {
            if ((bits >> b) & 1u) {
                if (count < max_out) {
                    out_addrs[count++] = gp->base + ((w * 32u + b) << 2);
                }
            }
        }
    }
    gp->dirty = 0u;
    return count;
}

uint16_t *emu_panel_frame_ptr(EmuPanel *p)
{
    return p->frame;
}

uint8_t *emu_panel_painted_ptr(EmuPanel *p)
{
    return p->painted;
}

uint64_t emu_panel_pixel_total(const EmuPanel *p)
{
    return p->pixel_total;
}

uint64_t emu_panel_out_of_range(const EmuPanel *p)
{
    return p->out_of_range;
}

uint32_t emu_panel_painted_count(const EmuPanel *p)
{
    uint32_t total = 0u;
    for (int i = 0; i < EMU_PANEL_PIXELS; i++) {
        total += p->painted[i];
    }
    return total;
}

uint32_t emu_panel_color_count(const EmuPanel *p)
{
    uint64_t seen[1024];
    memset(seen, 0, sizeof(seen));
    uint32_t count = 0u;
    for (int i = 0; i < EMU_PANEL_PIXELS; i++) {
        uint16_t color = p->frame[i];
        uint64_t bit = 1ULL << (color & 63u);
        if ((seen[color >> 6] & bit) == 0ULL) {
            seen[color >> 6] |= bit;
            count++;
        }
    }
    return count;
}

uint32_t emu_panel_group_count(const EmuPanel *p)
{
    return p->group_count;
}

uint32_t emu_panel_param_len(const EmuPanel *p)
{
    return p->param_len;
}

uint32_t emu_panel_window_count(const EmuPanel *p)
{
    return p->window_count;
}

uint32_t emu_panel_sweep_count(const EmuPanel *p)
{
    return p->sweep_count;
}

uint32_t emu_panel_lit_sweep_count(const EmuPanel *p)
{
    return p->lit_sweep_count;
}

void emu_panel_get_group(const EmuPanel *p, uint32_t index, uint8_t *cmd, uint32_t *offset,
                         uint32_t *count)
{
    const EmuGroup *g = &p->groups[index];
    *cmd = g->cmd;
    *offset = g->param_offset;
    *count = g->param_count;
}

const uint8_t *emu_panel_params_ptr(const EmuPanel *p)
{
    return p->params;
}

void emu_panel_get_window(const EmuPanel *p, uint32_t index, EmuWindow *out)
{
    *out = p->windows[index];
}

void emu_panel_get_sweep(const EmuPanel *p, uint32_t index, EmuSweep *out)
{
    *out = p->sweeps[index];
}

void emu_panel_get_bounds(const EmuPanel *p, int32_t *c0, int32_t *c1, int32_t *r0, int32_t *r1)
{
    *c0 = p->col0;
    *c1 = p->col1;
    *r0 = p->row0;
    *r1 = p->row1;
}

uint64_t emu_panel_dmac_pushed(const EmuPanel *p)
{
    return p->dmac_pushed_pixels;
}

uint32_t emu_panel_vop_pixel_mode(const EmuPanel *p)
{
    return p->pixel_mode;
}

static inline uint32_t core1_block_insns(EmuCore1 *c1, uint64_t addr, uint32_t size)
{
    if (addr < EMU_CORE1_CODE_SIZE && size <= EMU_CORE1_CODE_SIZE - addr && size <= 0xFFFFu) {
        uint32_t idx = (uint32_t)(addr >> 1);
        uint32_t cached = c1->block_cache[idx];
        if ((cached >> 16) == size && (cached & 0xFFFFu) != 0u) {
            return cached & 0xFFFFu;
        }
        uint32_t insns = count_thumb_insns(c1->code_buf + addr, size);
        c1->block_cache[idx] = (size << 16) | (insns & 0xFFFFu);
        return insns;
    }
    uint8_t buf[256];
    uint32_t len = size < sizeof(buf) ? size : (uint32_t)sizeof(buf);
    if (len >= 2u && uc_mem_read(c1->uc, addr, buf, len) == UC_ERR_OK) {
        return count_thumb_insns(buf, len);
    }
    return 1u;
}

static void decoder_add_clock_us(EmuDecoder *d, uint32_t mhz, double us)
{
    for (uint32_t i = 0; i < d->h.clock_count; i++) {
        if (d->h.clock_mhz[i] == mhz) {
            d->h.clock_us[i] += us;
            return;
        }
    }
    if (d->h.clock_count < 16u) {
        uint32_t idx = d->h.clock_count++;
        d->h.clock_mhz[idx] = mhz;
        d->h.clock_us[idx] = us;
    }
}

static void core1_block_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *ud)
{
    EmuCore1 *c1 = (EmuCore1 *)ud;
    EmuDecoder *d = c1->decoder;
    if (d != NULL) {
        if (d->h.resting || (d->use_limit && (d->h.time_us >= d->limit_us ||
                                              d->run_insns >= EMU_CORE1_RUN_INSN_LIMIT))) {
            uc_emu_stop(uc);
            return;
        }
        if (!d->use_limit && c1->slice_insns >= c1->slice_budget) {
            uc_emu_stop(uc);
            return;
        }
        uint32_t insns = core1_block_insns(c1, addr, size);
        c1->slice_insns += insns;
        d->h.instructions += insns;
        d->run_insns += insns;
        double dt = (double)insns / (double)d->h.mhz;
        d->h.time_us += dt;
        decoder_add_clock_us(d, d->h.mhz, dt);
        return;
    }
    if (c1->slice_insns >= c1->slice_budget) {
        uc_emu_stop(uc);
        return;
    }
    uint32_t insns = core1_block_insns(c1, addr, size);
    c1->slice_insns += insns;
}

static void core1_clock_write_cb(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
                                 int64_t value, void *ud)
{
    (void)uc;
    (void)type;
    (void)address;
    (void)size;
    EmuCore1 *c1 = (EmuCore1 *)ud;
    uint32_t v = (uint32_t)value;
    c1->clock = v;
    if (c1->clock_write_count == c1->clock_write_cap) {
        uint32_t next_cap = c1->clock_write_cap ? c1->clock_write_cap * 2u : 64u;
        uint32_t *buf = (uint32_t *)realloc(c1->clock_writes, next_cap * sizeof(uint32_t));
        if (buf != NULL) {
            c1->clock_writes = buf;
            c1->clock_write_cap = next_cap;
        }
    }
    if (c1->clock_write_count < c1->clock_write_cap) {
        c1->clock_writes[c1->clock_write_count++] = v;
    }
}

EmuCore1 *emu_core1_create(uc_engine *uc, void *shared_ram_ptr)
{
    EmuCore1 *c1 = (EmuCore1 *)calloc(1, sizeof(EmuCore1));
    if (c1 == NULL) {
        return NULL;
    }
    c1->uc = uc;
    c1->clock = 0x70001000u;
    c1->code_buf = (uint8_t *)alloc_page_aligned(EMU_CORE1_CODE_SIZE);
    c1->scb_buf = (uint8_t *)alloc_page_aligned(EMU_CORE1_SCB_SIZE);
    c1->periph_buf = (uint8_t *)alloc_page_aligned(EMU_CORE1_PERIPH_SIZE);
    if (shared_ram_ptr != NULL) {
        c1->is_shared = 1u;
        c1->priv_ram_buf = (uint8_t *)alloc_page_aligned(EMU_CORE1_SHARED_OFFSET);
        c1->shared_ram_buf = (uint8_t *)shared_ram_ptr;
    } else {
        c1->is_shared = 0u;
        c1->priv_ram_buf = (uint8_t *)alloc_page_aligned(EMU_CORE1_RAM_SIZE);
        c1->shared_ram_buf = c1->priv_ram_buf ? (c1->priv_ram_buf + EMU_CORE1_SHARED_OFFSET) : NULL;
    }
    if (c1->code_buf == NULL || c1->scb_buf == NULL || c1->periph_buf == NULL ||
        c1->priv_ram_buf == NULL) {
        free(c1->code_buf);
        free(c1->scb_buf);
        free(c1->periph_buf);
        free(c1->priv_ram_buf);
        free(c1);
        return NULL;
    }
    uint32_t rw = UC_PROT_READ | UC_PROT_WRITE;
    uint32_t private_size = c1->is_shared ? EMU_CORE1_SHARED_OFFSET : EMU_CORE1_RAM_SIZE;
    const EmuMapping regions[] = {
        {EMU_CORE1_CODE_BASE, EMU_CORE1_CODE_SIZE, UC_PROT_READ | UC_PROT_EXEC, c1->code_buf, NULL,
         NULL},
        {EMU_CORE1_RAM_BASE, private_size, rw, c1->priv_ram_buf, NULL, NULL},
        {EMU_CORE1_SCB_BASE, EMU_CORE1_SCB_SIZE, rw, c1->scb_buf, NULL, NULL},
        {EMU_CORE1_PERIPH_BASE, EMU_CORE1_PERIPH_SIZE, rw, c1->periph_buf, NULL, NULL},
        {EMU_CORE1_SHARED_BASE, EMU_CORE1_SHARED_SIZE, rw, c1->shared_ram_buf, NULL, NULL},
    };
    size_t count = sizeof(regions) / sizeof(regions[0]) - (c1->is_shared ? 0u : 1u);
    size_t mapped = map_regions(uc, regions, count);
    if (mapped != count ||
        uc_hook_add(uc, &c1->clock_hook, UC_HOOK_MEM_WRITE, (void *)core1_clock_write_cb, c1,
                    EMU_CORE1_CRU_CLOCK_SELECT, EMU_CORE1_CRU_CLOCK_SELECT) != UC_ERR_OK) {
        unmap_regions(uc, regions, mapped);
        free(c1->code_buf);
        free(c1->scb_buf);
        free(c1->periph_buf);
        free(c1->priv_ram_buf);
        free(c1);
        return NULL;
    }
    return c1;
}

void emu_core1_destroy(EmuCore1 *c1)
{
    if (c1 == NULL) {
        return;
    }
    free(c1->decoder);
    free(c1->clock_writes);
    free(c1->code_buf);
    free(c1->scb_buf);
    free(c1->periph_buf);
    free(c1->priv_ram_buf);
    free(c1);
}

void emu_core1_clear_cache(EmuCore1 *c1)
{
    memset(c1->block_cache, 0, sizeof(c1->block_cache));
}

uint32_t emu_core1_read_u32(const EmuCore1 *c1, uint32_t addr)
{
    if (addr >= EMU_CORE1_RAM_BASE && addr <= EMU_CORE1_SHARED_BASE - 4u) {
        return read_u32_le(c1->priv_ram_buf + (addr - EMU_CORE1_RAM_BASE));
    }
    if (addr >= EMU_CORE1_SHARED_BASE &&
        addr <= EMU_CORE1_SHARED_BASE + EMU_CORE1_SHARED_SIZE - 4u) {
        return read_u32_le(c1->shared_ram_buf + (addr - EMU_CORE1_SHARED_BASE));
    }
    if (addr >= EMU_CORE1_SCB_BASE && addr <= EMU_CORE1_SCB_BASE + EMU_CORE1_SCB_SIZE - 4u) {
        return read_u32_le(c1->scb_buf + (addr - EMU_CORE1_SCB_BASE));
    }
    uint32_t v = 0u;
    uc_mem_read(c1->uc, addr, &v, 4u);
    return v;
}

void emu_core1_write_u32(EmuCore1 *c1, uint32_t addr, uint32_t value)
{
    if (addr >= EMU_CORE1_RAM_BASE && addr <= EMU_CORE1_SHARED_BASE - 4u) {
        write_u32_le(c1->priv_ram_buf + (addr - EMU_CORE1_RAM_BASE), value);
        return;
    }
    if (addr >= EMU_CORE1_SHARED_BASE &&
        addr <= EMU_CORE1_SHARED_BASE + EMU_CORE1_SHARED_SIZE - 4u) {
        write_u32_le(c1->shared_ram_buf + (addr - EMU_CORE1_SHARED_BASE), value);
        return;
    }
    if (addr >= EMU_CORE1_SCB_BASE && addr <= EMU_CORE1_SCB_BASE + EMU_CORE1_SCB_SIZE - 4u) {
        write_u32_le(c1->scb_buf + (addr - EMU_CORE1_SCB_BASE), value);
        return;
    }
    uc_mem_write(c1->uc, addr, &value, 4u);
}

uint32_t emu_core1_run_budget(EmuCore1 *c1, uint32_t pc, uint64_t budget, uint64_t *out_insns)
{
    c1->slice_insns = 0ULL;
    c1->slice_budget = budget;
    c1->pc = pc;
    if (c1->decoder != NULL) {
        uc_emu_start(c1->uc, (uint64_t)(pc | 1u), 0xFFFFFFFEULL, 0ULL, 0u);
        uint64_t next_pc = 0;
        uc_reg_read(c1->uc, UC_ARM_REG_PC, &next_pc);
        c1->pc = (uint32_t)(next_pc | 1u);
        c1->instructions += c1->slice_insns;
        if (out_insns != NULL) {
            *out_insns = c1->slice_insns;
        }
        return c1->pc;
    }
    uc_emu_start(c1->uc, (uint64_t)(pc | 1u), 0xFFFFFFFEULL, 0ULL, (size_t)budget);
    uint64_t next_pc = 0;
    uc_reg_read(c1->uc, UC_ARM_REG_PC, &next_pc);
    c1->pc = (uint32_t)(next_pc | 1u);
    c1->instructions += budget;
    if (out_insns != NULL) {
        *out_insns = budget;
    }
    return c1->pc;
}

uint32_t emu_core1_clock(const EmuCore1 *c1)
{
    return c1->clock;
}

uint32_t emu_core1_clock_write_count(const EmuCore1 *c1)
{
    return c1->clock_write_count;
}

const uint32_t *emu_core1_clock_writes_ptr(const EmuCore1 *c1)
{
    return c1->clock_writes;
}

void emu_core1_clear_clock_writes(EmuCore1 *c1)
{
    c1->clock_write_count = 0u;
}

void *emu_core1_shared_ptr(EmuCore1 *c1)
{
    return c1->shared_ram_buf;
}

static inline double decoder_period_us(const EmuDecoder *d)
{
    return (double)(d->h.reload + 1u) / (double)d->h.mhz;
}

static inline uint32_t decoder_current(const EmuDecoder *d)
{
    if (d->h.reload == 0u) {
        return 0u;
    }
    int64_t into = (int64_t)((d->h.time_us - d->h.period_start) * (double)d->h.mhz);
    int64_t mod = (int64_t)d->h.reload + 1;
    int64_t rem = into % mod;
    if (rem < 0) {
        rem += mod;
    }
    return (uint32_t)((int64_t)d->h.reload - rem);
}

void emu_decoder_deliver(EmuDecoder *d)
{
    double period = decoder_period_us(d);
    if (d->h.reload == 0u || period <= 0.0) {
        return;
    }
    while (d->h.time_us - d->h.period_start >= period) {
        d->h.period_start += period;
        uint64_t add = (uint64_t)d->h.reload + 1ULL;
        if (d->h.in_units && d->h.mhz != 100u) {
            add = (add * 100ULL) / (uint64_t)d->h.mhz;
        }
        if (d->h.counter != 0u) {
            uint32_t elapsed = (emu_core1_read_u32(d->core1, d->h.counter) + (uint32_t)add);
            emu_core1_write_u32(d->core1, d->h.counter, elapsed);
            emu_core1_write_u32(d->core1, EMU_CORE1_SHARED_CYCLES, elapsed);
        }
    }
}

static double decoder_wake_at(const EmuDecoder *d)
{
    double period = decoder_period_us(d);
    if (period <= 0.0) {
        return d->h.time_us;
    }
    double wraps = floor((d->h.time_us - d->h.period_start) / period);
    return d->h.period_start + period * (wraps + 1.0);
}

static void decoder_sync_pll(EmuDecoder *d)
{
    uint8_t published[4] = {0};
    if (d->host_core1_ram != NULL) {
        uint32_t offset =
            EMU_CORE1_SHARED_OFFSET + EMU_CORE1_SHARED_PLL_MHZ - EMU_CORE1_SHARED_BASE;
        memcpy(published, d->host_core1_ram + offset, sizeof published);
    } else if (d->host_uc != NULL) {
        uc_mem_read(d->host_uc, EMU_CORE1_SHARED_PLL_MHZ, published, sizeof published);
    }
    uint32_t mhz = read_u32_le(published);
    if (mhz < d->h.divider || mhz == d->h.pll_mhz) {
        return;
    }
    emu_decoder_deliver(d);
    uint32_t previous = d->h.mhz;
    d->h.pll_mhz = mhz;
    d->h.mhz = mhz / d->h.divider;
    double elapsed = d->h.time_us - d->h.period_start;
    d->h.period_start = d->h.time_us - elapsed * previous / d->h.mhz;
    emu_core1_write_u32(d->core1, EMU_CORE1_SHARED_PLL_MHZ, mhz);
    if (!d->h.awake) {
        d->h.next_wake_us = decoder_wake_at(d);
    }
}

static void decoder_wfi_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *ud)
{
    (void)addr;
    (void)size;
    EmuDecoder *d = (EmuDecoder *)ud;
    d->h.resting = 1u;
    uc_emu_stop(uc);
}

static void decoder_read_cur_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                                int64_t value, void *ud)
{
    (void)uc;
    (void)type;
    (void)addr;
    (void)size;
    (void)value;
    EmuDecoder *d = (EmuDecoder *)ud;
    emu_decoder_deliver(d);
    uint32_t cur = decoder_current(d);
    write_u32_le(d->core1->scb_buf + (EMU_CORE1_SYSTICK_CURRENT - EMU_CORE1_SCB_BASE), cur);
}

static void decoder_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                             int64_t value, void *ud)
{
    (void)uc;
    (void)type;
    (void)size;
    EmuDecoder *d = (EmuDecoder *)ud;
    emu_decoder_deliver(d);
    if (addr == EMU_CORE1_SYSTICK_RELOAD) {
        d->h.reload = (uint32_t)(value & 0xFFFFFFu);
    } else if (addr == EMU_CORE1_SYSTICK_CURRENT) {
        d->h.period_start = d->h.time_us;
    } else if ((uint64_t)value & 0x70000000ULL) {
        d->h.divider = (((uint32_t)value >> 12) & 7u) + 1u;
        d->h.mhz = d->h.pll_mhz / d->h.divider;
    }
}

static void decoder_store_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                             int64_t value, void *ud)
{
    (void)uc;
    (void)type;
    (void)value;
    EmuDecoder *d = (EmuDecoder *)ud;
    uint32_t off = (uint32_t)(addr - EMU_CORE1_CONTROL);
    for (int i = 0; i < size && off + (uint32_t)i < EMU_CORE1_CONTROL_BYTES; i++) {
        uint32_t idx = off + (uint32_t)i;
        d->stored_mask[idx >> 6] |= (1ULL << (idx & 63u));
    }
}

static void delete_hook(uc_engine *uc, uc_hook hook)
{
    if (hook != 0u) {
        uc_hook_del(uc, hook);
    }
}

void emu_decoder_destroy(EmuDecoder *d)
{
    EmuCore1 *c1 = d->core1;
    delete_hook(c1->uc, d->wfi_hook);
    delete_hook(c1->uc, d->read_cur_hook);
    delete_hook(c1->uc, d->write_sys_hook);
    delete_hook(c1->uc, d->write_cru_hook);
    delete_hook(c1->uc, d->store_hook);
    delete_hook(c1->uc, c1->block_hook);
    c1->block_hook = 0u;
    c1->decoder = NULL;
    free(d);
}

EmuDecoder *emu_core1_decoder(EmuCore1 *c1)
{
    return c1->decoder;
}

EmuDecoder *emu_decoder_attach(EmuCore1 *c1, uc_engine *host_uc, void *host_core1_ram, uint32_t wfi,
                               uint32_t counter, uint8_t in_units, uint32_t ring_bytes)
{
    if (c1->decoder != NULL) {
        return c1->decoder;
    }
    EmuDecoder *d = (EmuDecoder *)calloc(1, sizeof(EmuDecoder));
    if (d == NULL) {
        return NULL;
    }
    d->core1 = c1;
    d->host_uc = host_uc;
    d->host_core1_ram = (uint8_t *)host_core1_ram;
    d->h.mhz = 100u;
    d->h.pll_mhz = EMU_CORE1_PLL_MHZ;
    d->h.divider = 2u;
    d->h.wfi = wfi;
    d->h.counter = counter;
    d->h.in_units = in_units;
    d->h.ring_bytes = ring_bytes;
    uc_engine *uc = c1->uc;
    if (uc_hook_add(uc, &c1->block_hook, UC_HOOK_BLOCK, (void *)core1_block_cb, c1, 1u, 0u) !=
            UC_ERR_OK ||
        uc_hook_add(uc, &d->wfi_hook, UC_HOOK_CODE, (void *)decoder_wfi_cb, d, wfi, wfi) !=
            UC_ERR_OK ||
        uc_hook_add(uc, &d->read_cur_hook, UC_HOOK_MEM_READ, (void *)decoder_read_cur_cb, d,
                    EMU_CORE1_SYSTICK_CURRENT, EMU_CORE1_SYSTICK_CURRENT) != UC_ERR_OK ||
        uc_hook_add(uc, &d->write_sys_hook, UC_HOOK_MEM_WRITE, (void *)decoder_write_cb, d,
                    EMU_CORE1_SYSTICK_RELOAD, EMU_CORE1_SYSTICK_CURRENT) != UC_ERR_OK ||
        uc_hook_add(uc, &d->write_cru_hook, UC_HOOK_MEM_WRITE, (void *)decoder_write_cb, d,
                    EMU_CORE1_CRU_CLOCK_SELECT, EMU_CORE1_CRU_CLOCK_SELECT) != UC_ERR_OK ||
        uc_hook_add(uc, &d->store_hook, UC_HOOK_MEM_WRITE, (void *)decoder_store_cb, d,
                    EMU_CORE1_CONTROL,
                    EMU_CORE1_CONTROL + EMU_CORE1_CONTROL_BYTES - 1u) != UC_ERR_OK) {
        emu_decoder_destroy(d);
        return NULL;
    }
    c1->decoder = d;
    emu_core1_clear_cache(c1);
    return d;
}

EmuDecoderHeader *emu_decoder_header(EmuDecoder *d)
{
    return &d->h;
}

void emu_decoder_wake(EmuDecoder *d)
{
    double rest_until = d->h.next_wake_us;
    decoder_add_clock_us(d, d->h.mhz, rest_until - d->h.time_us);
    d->h.time_us = rest_until;
    emu_decoder_deliver(d);
    d->h.wakes++;
    d->stored_mask[0] = 0ULL;
    d->stored_mask[1] = 0ULL;
    d->h.awake = 1u;
}

int emu_decoder_run_until(EmuDecoder *d, double limit_us)
{
    EmuCore1 *c1 = d->core1;
    d->h.resting = 0u;
    double started = d->h.time_us;
    d->limit_us = limit_us;
    d->run_insns = 0ULL;
    d->use_limit = 1u;
    if (d->h.time_us < limit_us) {
        uc_emu_start(c1->uc, (uint64_t)(c1->pc | 1u), 0xFFFFFFFEULL, 0ULL, 0u);
    }
    d->use_limit = 0u;
    d->h.busy_us += d->h.time_us - started;
    if (d->h.resting) {
        c1->pc = (d->h.wfi + 2u) | 1u;
        return 0;
    }
    uint64_t pc = 0;
    uc_reg_read(c1->uc, UC_ARM_REG_PC, &pc);
    c1->pc = (uint32_t)(pc | 1u);
    if (d->run_insns >= EMU_CORE1_RUN_INSN_LIMIT && d->h.time_us < limit_us) {
        return -1;
    }
    return 0;
}

void emu_decoder_publish(EmuDecoder *d)
{
    uint64_t m0 = d->stored_mask[0];
    uint64_t m1 = d->stored_mask[1];
    if (m0 == 0ULL && m1 == 0ULL) {
        return;
    }
    const uint8_t *c1_shared = d->core1->shared_ram_buf;
    if (d->host_core1_ram != NULL) {
        uint8_t *host_shared = d->host_core1_ram + EMU_CORE1_SHARED_OFFSET;
        memcpy(host_shared + (EMU_CORE1_SAMPLES - EMU_CORE1_SHARED_BASE),
               c1_shared + (EMU_CORE1_SAMPLES - EMU_CORE1_SHARED_BASE), EMU_CORE1_SAMPLE_BYTES);
        uint32_t ctrl_off = EMU_CORE1_CONTROL - EMU_CORE1_SHARED_BASE;
        for (uint32_t i = 0; i < EMU_CORE1_CONTROL_BYTES; i++) {
            uint64_t mask = (i < 64u) ? m0 : m1;
            if ((mask >> (i & 63u)) & 1ULL) {
                host_shared[ctrl_off + i] = c1_shared[ctrl_off + i];
            }
        }
    } else if (d->host_uc != NULL) {
        uc_mem_write(d->host_uc, EMU_CORE1_SAMPLES,
                     c1_shared + (EMU_CORE1_SAMPLES - EMU_CORE1_SHARED_BASE),
                     EMU_CORE1_SAMPLE_BYTES);
        uint32_t ctrl_off = EMU_CORE1_CONTROL - EMU_CORE1_SHARED_BASE;
        for (uint32_t i = 0; i < EMU_CORE1_CONTROL_BYTES; i++) {
            uint64_t mask = (i < 64u) ? m0 : m1;
            if ((mask >> (i & 63u)) & 1ULL) {
                uc_mem_write(d->host_uc, EMU_CORE1_CONTROL + i, &c1_shared[ctrl_off + i], 1u);
            }
        }
    }
    d->stored_mask[0] = 0ULL;
    d->stored_mask[1] = 0ULL;
}

void emu_decoder_copy_input(EmuDecoder *d)
{
    uint8_t *c1_shared = d->core1->shared_ram_buf;
    uint32_t ctrl_off = EMU_CORE1_CONTROL - EMU_CORE1_SHARED_BASE;
    uint32_t ring_off = EMU_CORE1_RING - EMU_CORE1_SHARED_BASE;
    uint32_t ring_bytes = d->h.ring_bytes;
    if (d->host_core1_ram != NULL) {
        const uint8_t *host_shared = d->host_core1_ram + EMU_CORE1_SHARED_OFFSET;
        memcpy(c1_shared + ctrl_off, host_shared + ctrl_off, EMU_CORE1_CONTROL_BYTES);
        uint32_t gen = read_u32_le(c1_shared + ctrl_off);
        uint32_t written = read_u32_le(c1_shared + ctrl_off + EMU_CORE1_WRITTEN_OFFSET);
        int has_start = (d->h.has_copied && d->h.copied_gen == gen);
        uint32_t start = d->h.copied_written;
        if (!has_start || written < start || (written - start) >= ring_bytes) {
            memcpy(c1_shared + ring_off, host_shared + ring_off, ring_bytes);
        } else if (written > start) {
            uint32_t offset = start % ring_bytes;
            uint32_t delta = written - start;
            uint32_t first = (delta < ring_bytes - offset) ? delta : (ring_bytes - offset);
            memcpy(c1_shared + ring_off + offset, host_shared + ring_off + offset, first);
            if (delta > first) {
                memcpy(c1_shared + ring_off, host_shared + ring_off, delta - first);
            }
        }
        d->h.has_copied = 1u;
        d->h.copied_gen = gen;
        d->h.copied_written = written;
    } else if (d->host_uc != NULL) {
        uc_mem_read(d->host_uc, EMU_CORE1_CONTROL, c1_shared + ctrl_off, EMU_CORE1_CONTROL_BYTES);
        uint32_t gen = read_u32_le(c1_shared + ctrl_off);
        uint32_t written = read_u32_le(c1_shared + ctrl_off + EMU_CORE1_WRITTEN_OFFSET);
        int has_start = (d->h.has_copied && d->h.copied_gen == gen);
        uint32_t start = d->h.copied_written;
        if (!has_start || written < start || (written - start) >= ring_bytes) {
            uc_mem_read(d->host_uc, EMU_CORE1_RING, c1_shared + ring_off, ring_bytes);
        } else if (written > start) {
            uint32_t offset = start % ring_bytes;
            uint32_t delta = written - start;
            uint32_t first = (delta < ring_bytes - offset) ? delta : (ring_bytes - offset);
            uc_mem_read(d->host_uc, EMU_CORE1_RING + offset, c1_shared + ring_off + offset, first);
            if (delta > first) {
                uc_mem_read(d->host_uc, EMU_CORE1_RING, c1_shared + ring_off, delta - first);
            }
        }
        d->h.has_copied = 1u;
        d->h.copied_gen = gen;
        d->h.copied_written = written;
    }
}

int emu_decoder_advance_to(EmuDecoder *d, double now_us)
{
    int rc = emu_decoder_run_until(d, now_us);
    if (rc != 0) {
        return rc;
    }
    emu_decoder_deliver(d);
    emu_decoder_publish(d);
    if (d->h.resting) {
        d->h.awake = 0u;
        d->h.next_wake_us = decoder_wake_at(d);
    }
    return 0;
}

int emu_decoder_step(EmuDecoder *d, double now_us)
{
    decoder_sync_pll(d);
    for (int i = 0; i < EMU_CORE1_WAKES_PER_STEP; i++) {
        if (!d->h.awake) {
            if (now_us < d->h.next_wake_us) {
                break;
            }
            emu_decoder_wake(d);
        }
        int rc = emu_decoder_advance_to(d, now_us);
        if (rc != 0) {
            return rc;
        }
        if (d->h.awake) {
            break;
        }
    }
    emu_decoder_copy_input(d);
    return 0;
}
