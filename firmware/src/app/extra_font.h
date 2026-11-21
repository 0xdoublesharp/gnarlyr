#pragma once

#include "hal/storage.h"
#include "ui/theme.h"

void extra_font_open(HalFile *file);
void extra_font_close(HalFile *file);
void extra_font_hold_for_usb(const Theme *theme);
