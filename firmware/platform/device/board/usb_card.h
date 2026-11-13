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

typedef struct UsbCardSpeed {
    uint8_t fast;
    uint8_t fallbacks;
    uint16_t fifo_words[USB_CARD_COUNT];
    uint32_t hcon[USB_CARD_COUNT];
} UsbCardSpeed;

void usb_card_prepare(const EmmcLayout *internal);
void usb_card_speed_up(bool internal_ready, uint32_t *scratch);
void usb_card_speed_down(void);
bool usb_card_slow_down(uint8_t lun);
UsbCardSpeed usb_card_speed(void);
