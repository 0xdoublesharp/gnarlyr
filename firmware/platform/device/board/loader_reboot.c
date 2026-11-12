#include <stdbool.h>

#include "board.h"
#include "emmc.h"
#include "registers.h"

#define PMU_BASE 0x50020000u
#define PMU_BOOT_FLAG (PMU_BASE + 0x4Cu)
#define PMU_BOOT_LOADER 0xFFFF18AFu
#define PMU_BOOT_FLAG_MASK 0xFFEFu
#define PMU_BOOT_FLAG_VALUE 0x18AFu
#define PMU_BOOT_FLAG_CLEAR 0xFFFF0000u
#define CRU_ALL_ON 0xFFFF0000u
#define CRU_GATES_FIRST 0x80u
#define CRU_GATES_LAST 0xA4u
#define CRU_RESETS_FIRST 0xC0u
#define CRU_RESETS_LAST 0xCCu
#define CRU_SETTLE 0x80008000u
#define CRU_SETTLE_OFFSET 0xA0u
#define GRF_PLL_A 0xB8u
#define GRF_PLL_B 0xB4u
#define GRF_PLL_C 0x98u
#define GRF_BOOT_SELECT 0xC4u
#define AIRCR_RESET 0x05FA0007u

enum {
    GATE_SETTLE_LOOPS = 0x4B0,
    PLL_SETTLE_LOOPS = 100
};

static void spin(volatile int loops)
{
    for (volatile int index = 0; index < loops; index++) {
    }
}

static void quiesce_interrupts(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    register_write(SYSTICK_CTRL, 0);
    register_write(NVIC_ICER0, 0xFFFFFFFFu);
    register_write(NVIC_ICPR0, 0xFFFFFFFFu);
    register_write(NVIC_ICER1, 0xFFFFFFFFu);
    register_write(NVIC_ICPR1, 0xFFFFFFFFu);
}

static void enable_all_blocks(void)
{
    for (uint32_t offset = CRU_GATES_FIRST; offset <= CRU_GATES_LAST; offset += 4) {
        register_write(CRU_BASE + offset, CRU_ALL_ON);
    }
    register_write(CRU_BASE + CRU_SETTLE_OFFSET, CRU_SETTLE);
    spin(GATE_SETTLE_LOOPS);
    for (uint32_t offset = CRU_RESETS_FIRST; offset <= CRU_RESETS_LAST; offset += 4) {
        register_write(CRU_BASE + offset, CRU_ALL_ON);
    }
}

static void reset_pll(void)
{
    register_write(GRF_BASE + GRF_PLL_A, 0x00010000u);
    register_write(GRF_BASE + GRF_PLL_B, 0x000E0002u);
    register_write(GRF_BASE + GRF_PLL_C, 0x00040004u);
    register_write(GRF_BASE + GRF_PLL_B, 0x00010000u);
    spin(PLL_SETTLE_LOOPS);
}

__attribute__((noinline, noreturn)) static void reset_with(uint32_t flag, bool loader)
{
    quiesce_interrupts();
    enable_all_blocks();
    reset_pll();
    if (loader) {
        register_write(GRF_BASE + GRF_BOOT_SELECT, 0x01000000u);
    }
    register_write(SCB_VTOR, 0);
    register_write(PMU_BOOT_FLAG, flag);
    __asm__ volatile("dsb" ::: "memory");
    register_write(SCB_AIRCR, AIRCR_RESET);
    for (;;) {
    }
}

void board_loader_reboot(void)
{
    board_start_record_step(HAL_START_STEP_LOADER);
    reset_with(PMU_BOOT_LOADER, true);
}

void board_restart_clean(void)
{
    emmc_quiesce();
    board_restart();
}

void board_restart(void)
{
    board_start_record_power_off();
    reset_with(PMU_BOOT_FLAG_CLEAR, false);
}

void board_loader_flag_clear(void)
{
    if ((register_read(PMU_BOOT_FLAG) & PMU_BOOT_FLAG_MASK) == PMU_BOOT_FLAG_VALUE) {
        register_write(PMU_BOOT_FLAG, PMU_BOOT_FLAG_CLEAR);
    }
}
