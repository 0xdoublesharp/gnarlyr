#include <stdint.h>

#include "device/core1_control.h"
#include "idle.h"
#include "registers.h"

#define CORE1_STACK_TOP 0x0102E000u
#define ALL_INTERRUPTS 0xFFFFFFFFu
#define SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK 0x7u

typedef void (*Handler)(void);

static volatile uint32_t ticks;
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
    ticks++;
    register_write(CORE1_SHARED_TICKS, ticks);
}

static uint32_t cycles_now(void)
{
    uint32_t tick;
    uint32_t current;
    do {
        tick = ticks;
        current = register_read(SYSTICK_CURRENT);
    } while (tick != ticks);
    return tick * CORE1_TICK_CYCLES + (CORE1_TICK_CYCLES - 1u - current);
}

void core1_idle_start(void)
{
    register_write(SYSTICK_CTRL, 0);
    ticks = 0;
    idle_cycles = 0;
    register_write(CORE1_SHARED_TICKS, 0);
    register_write(CORE1_SHARED_IDLE_CYCLES, 0);
    register_write(SYSTICK_RELOAD, CORE1_TICK_CYCLES - 1u);
    register_write(SYSTICK_CURRENT, 0);
    register_write(SYSTICK_CTRL, SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK);
    __asm__ volatile("cpsie i" ::: "memory");
}

void core1_idle(void)
{
    uint32_t start = cycles_now();
    __asm__ volatile("wfi" ::: "memory");
    uint32_t idle = cycles_now() - start;
    if (idle < CORE1_TICK_CYCLES * 2u) {
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
