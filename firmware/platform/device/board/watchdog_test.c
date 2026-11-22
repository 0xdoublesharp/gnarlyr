#include "board.h"
#include "hal/system.h"

void hal_system_watchdog_test(void)
{
    board_start_record_step(HAL_START_STEP_WATCHDOG_TEST);
    __asm__ volatile("cpsid i" ::: "memory");
    for (;;) {
    }
}
