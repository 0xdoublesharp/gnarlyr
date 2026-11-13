#include <stdbool.h>
#include <stdint.h>

#include "device/clock_governor.h"
#include "device/core1_control.h"
#include "device/core1_pace.h"
#include "idle.h"
#include "registers.h"

#define CORE1_STACK_TOP 0x0102E000u
#define ALL_INTERRUPTS 0xFFFFFFFFu
#define IDLE_LIMIT 0x80000000u
#define SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK 0x7u
#define SYSTICK_COUNTED (1u << 16)
#define CRU_CLOCK_SELECT 0x40180014u
#define CORE1_DIVIDER_WRITE 0x70000000u
#define CORE1_DIVIDER_SHIFT 12

typedef void (*Handler)(void);

static volatile uint32_t elapsed_units;
static uint32_t idle_units;
static uint32_t clock_mhz;
static uint32_t timer_us;
static ClockGovernor governor;

extern uint32_t core1_bss_start;
extern uint32_t core1_bss_end;

void core1_reset(void);
void core1_default_handler(void);
void core1_main(void);
void core1_tick(void);

__attribute__((section(".core1_vectors"), used)) static const Handler CORE1_VECTORS[16] = {
    (Handler)CORE1_STACK_TOP,
    core1_reset,
    core1_default_handler,
    core1_default_handler,
    core1_default_handler,
    core1_default_handler,
    core1_default_handler,
    0,
    0,
    0,
    0,
    core1_default_handler,
    core1_default_handler,
    0,
    core1_default_handler,
    core1_tick,
};

void core1_default_handler(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void core1_tick(void)
{
    (void)register_read(SYSTICK_CTRL);
    elapsed_units += core1_pace_units(register_read(SYSTICK_RELOAD) + 1u, clock_mhz);
    register_write(CORE1_SHARED_CYCLES, elapsed_units);
}

static uint32_t units_now(void)
{
    uint32_t base;
    uint32_t reload;
    uint32_t current;
    do {
        base = elapsed_units;
        reload = register_read(SYSTICK_RELOAD);
        current = register_read(SYSTICK_CURRENT);
    } while (base != elapsed_units);
    return base + core1_pace_units(reload - current, clock_mhz);
}

static void fold_period(void)
{
    uint32_t reload = register_read(SYSTICK_RELOAD);
    uint32_t current = register_read(SYSTICK_CURRENT);
    if ((register_read(SYSTICK_CTRL) & SYSTICK_COUNTED) != 0) {
        register_write(SCB_ICSR, SCB_ICSR_PENDSTCLR);
        current = register_read(SYSTICK_CURRENT);
        elapsed_units += core1_pace_units(reload + 1u, clock_mhz);
    }
    elapsed_units += core1_pace_units(reload - current, clock_mhz);
}

static void use_timer(uint32_t mhz, uint32_t microseconds)
{
    if (mhz == clock_mhz && microseconds == timer_us) {
        return;
    }
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(primask)::"memory");
    fold_period();
    if (mhz != clock_mhz) {
        register_write(CRU_CLOCK_SELECT, CORE1_DIVIDER_WRITE | core1_pace_divider_field(mhz)
                                                                   << CORE1_DIVIDER_SHIFT);
        __asm__ volatile("dsb\nisb" ::: "memory");
        clock_mhz = mhz;
    }
    register_write(SYSTICK_RELOAD, mhz * microseconds - 1u);
    register_write(SYSTICK_CURRENT, 0);
    timer_us = microseconds;
    register_write(CORE1_SHARED_CYCLES, elapsed_units);
    __asm__ volatile("msr primask, %0" ::"r"(primask) : "memory");
}

static ClockGovernorReading reading(void)
{
    return (ClockGovernorReading){.now_us = units_now(), .idle = idle_units};
}

void core1_request_clock(uint32_t mhz)
{
    clock_governor_start(&governor, core1_pace_limits(mhz), reading());
    use_timer(core1_pace_mhz(governor.divider), timer_us);
}

void core1_idle_start(void)
{
    register_write(SYSTICK_CTRL, 0);
    elapsed_units = 0;
    idle_units = 0;
    clock_mhz = CORE1_PACE_IDLE_MHZ;
    timer_us = CORE1_PACE_REST_US;
    register_write(CORE1_SHARED_CYCLES, 0);
    register_write(CORE1_SHARED_IDLE_CYCLES, 0);
    register_write(SYSTICK_RELOAD, clock_mhz * timer_us - 1u);
    register_write(SYSTICK_CURRENT, 0);
    clock_governor_start(&governor, core1_pace_limits(CORE1_PACE_IDLE_MHZ), reading());
    register_write(SYSTICK_CTRL, SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK);
    __asm__ volatile("cpsie i" ::: "memory");
}

static void follow_load(void)
{
    if (clock_governor_update(&governor, reading())) {
        use_timer(core1_pace_mhz(governor.divider), timer_us);
    }
}

void core1_idle(const Core1Stream *stream)
{
    bool waiting = core1_stream_waiting_for_room(stream);
    uint32_t running = clock_mhz;
    uint32_t start = units_now();
    use_timer(core1_pace_rest_mhz(running), core1_pace_rest_us(waiting));
    __asm__ volatile("wfi" ::: "memory");
    use_timer(running, timer_us);
    uint32_t idle = units_now() - start;
    if (idle < IDLE_LIMIT) {
        idle_units += idle;
        register_write(CORE1_SHARED_IDLE_CYCLES, idle_units);
    }
    follow_load();
}

static void zero_bss(void)
{
    for (uint32_t *word = &core1_bss_start; word < &core1_bss_end; word++) {
        *word = 0;
    }
}

void core1_reset(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    register_write(SYSTICK_CTRL, 0);
    register_write(NVIC_ICER0, ALL_INTERRUPTS);
    register_write(NVIC_ICER1, ALL_INTERRUPTS);
    register_write(NVIC_ICPR0, ALL_INTERRUPTS);
    register_write(NVIC_ICPR1, ALL_INTERRUPTS);
    register_write(SCB_VTOR, 0);
    zero_bss();
    core1_main();
    core1_default_handler();
}
