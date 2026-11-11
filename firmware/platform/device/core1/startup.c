#include <stdint.h>

#include "idle.h"
#include "registers.h"

#define CORE1_STACK_TOP 0x0102E000u
#define ALL_INTERRUPTS 0xFFFFFFFFu
#define IDLE_TICKS 100000u
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
}

void core1_idle_start(void)
{
    register_write(SYSTICK_CTRL, 0);
    register_write(SYSTICK_RELOAD, IDLE_TICKS - 1);
    register_write(SYSTICK_CURRENT, 0);
    register_write(SYSTICK_CTRL, SYSTICK_ENABLE_INTERRUPT_CORE_CLOCK);
    __asm__ volatile("cpsie i" ::: "memory");
}

void core1_idle(void)
{
    if (requested_mhz == 200) {
        clock_mhz(100);
    }
    __asm__ volatile("wfi" ::: "memory");
    if (requested_mhz == 200) {
        clock_mhz(200);
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
