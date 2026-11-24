#include <stdint.h>

#include "board.h"
#include "core/seam.h"
#include "device/start_record.h"
#include "hal/system.h"
#include "registers.h"
#include "trial.h"

#define SCB_CFSR 0xE000ED28u
#define SCB_HFSR 0xE000ED2Cu

enum {
    STACKED_LR_WORD = 5,
    STACKED_PC_WORD = 6,
    FAULT_POLL_MS = 10
};

__attribute__((section(".noinit"))) static StartRecord record;
StartInfo board_start_info;

void board_start_record_boot(void)
{
    board_start_info = start_record_boot(&record);
}

CORE_SEAM bool board_start_after_loader(void)
{
    return board_start_info.last_step == HAL_START_STEP_LOADER;
}

void board_start_record_tick(uint32_t uptime_ms)
{
    start_record_tick(&record, uptime_ms);
}

void board_start_record_power_off(void)
{
    start_record_power_off(&record);
}

void board_start_record_step(HalStartStep step)
{
    start_record_step_begin(&record, step);
}

void board_start_record_step_done(void)
{
    start_record_step_end(&record);
}

void board_start_record_battery(uint32_t millivolts)
{
    start_record_battery(&record, millivolts);
}

BoardRunSnapshot board_start_record_snapshot(void)
{
    return (BoardRunSnapshot){.uptime_ms = record.uptime_ms,
                              .step = record.step,
                              .step_finished = record.step_finished != 0,
                              .millivolts = record.millivolts};
}

__attribute__((used)) void device_fault_handler(const uint32_t *frame)
{
    start_record_fault(&record, register_read(SCB_CFSR), register_read(SCB_HFSR),
                       frame[STACKED_PC_WORD]);
    board_panic_fault(frame, register_read(SCB_CFSR));
    uint32_t now_ms = 0;
    for (;;) {
        if (!board_trial_running()) {
            board_watchdog_kick();
        }
        board_panic_poll_power(now_ms);
        board_panic_delay_ms(FAULT_POLL_MS);
        now_ms += FAULT_POLL_MS;
    }
}

__attribute__((naked)) void device_fault_entry(void)
{
    __asm__ volatile("mrs r0, msp\n\tb device_fault_handler");
}
