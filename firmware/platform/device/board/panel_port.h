#pragma once

#include <stdint.h>

#include "device/panel.h"

void board_panel_start(void);
void board_panel_sleep(void);
void board_panel_wake(void);
void board_panel_set_rotation(PanelRotation rotation);
void board_panel_write(PanelWindow window, const uint16_t *pixels);
void board_panel_after_clock_change(void);
uint32_t board_panel_push_fallbacks(void);
