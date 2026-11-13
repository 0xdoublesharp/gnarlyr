#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/emmc_layout.h"
#include "device/msc.h"

enum {
    USB_CARD_INTERNAL = 0,
    USB_CARD_SD = 1,
    USB_CARD_COUNT = 2
};

extern const MscStream USB_CARD_STREAM;

void usb_card_prepare(const EmmcLayout *internal);
