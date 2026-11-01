#pragma once

#include <stdint.h>

#include "device/panel.h"

void board_panel_start(void);
void board_panel_write(PanelWindow window, const uint16_t *pixels);
