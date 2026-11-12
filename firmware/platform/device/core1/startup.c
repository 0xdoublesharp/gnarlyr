#include <stdint.h>

#include "device/core1_control.h"
#include "idle.h"
#include "registers.h"

#define CORE1_STACK_TOP 0x0102E000u
#define ALL_INTERRUPTS 0xFFFFFFFFu
#define IDLE_TICKS 500000u
#define IDLE_LIMIT 0x80000000u
#define SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK 0x7u
#define CRU_CLOCK_SELECT 0x40180014u

static uint32_t requested_mhz;

static void clock_mhz(uint32_t mhz)
{
    register_write(CRU_CLOCK_SELECT, 0x70000000u | (mhz == 200 ? 0u : 0x1000u));
    __asm__ volatile("dsb\nisb" ::: "memory");
    register_write(SYSTICK_RELOAD, mhz * 1000u - 1u);
}

void core1_request_clock(uint32_t mhz)
{
    requested_mhz = mhz == 200 ? 200u : 100u;
    clock_mhz(requested_mhz);
}

typedef void (*Handler)(void);

static volatile uint32_t elapsed_cycles;
static uint32_t idle_cycles;

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
    elapsed_cycles += register_read(SYSTICK_RELOAD) + 1u;
    register_write(CORE1_SHARED_CYCLES, elapsed_cycles);
}

static uint32_t cycles_now(void)
{
    uint32_t base;
    uint32_t reload;
    uint32_t current;
    do {
        base = elapsed_cycles;
        reload = register_read(SYSTICK_RELOAD);
        current = register_read(SYSTICK_CURRENT);
    } while (base != elapsed_cycles);
    return base + reload - current;
}

void core1_idle_start(void)
{
    register_write(SYSTICK_CTRL, 0);
    elapsed_cycles = 0;
    idle_cycles = 0;
    register_write(CORE1_SHARED_CYCLES, 0);
    register_write(CORE1_SHARED_IDLE_CYCLES, 0);
    register_write(SYSTICK_RELOAD, IDLE_TICKS - 1u);
    register_write(SYSTICK_CURRENT, 0);
    register_write(SYSTICK_CTRL, SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK);
    __asm__ volatile("cpsie i" ::: "memory");
}

void core1_idle(void)
{
    uint32_t start = cycles_now();
    if (requested_mhz == 200) {
        clock_mhz(100);
    }
    __asm__ volatile("wfi" ::: "memory");
    if (requested_mhz == 200) {
        clock_mhz(200);
    }
    uint32_t idle = cycles_now() - start;
    if (idle < IDLE_LIMIT) {
        idle_cycles += idle;
        register_write(CORE1_SHARED_IDLE_CYCLES, idle_cycles);
    }
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
