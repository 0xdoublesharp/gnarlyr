#pragma once

#include <stdint.h>

void board_trial_escape_check(void);
void board_trial_boot(void);
void board_trial_report(void);
void board_trial_settle(void);
void board_trial_session_starting(void);
void board_trial_session_ended(void);
void board_trial_poll_power(uint32_t now_ms);
