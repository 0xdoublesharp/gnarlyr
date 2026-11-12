#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/usb_audio.h"

enum {
    USB_FIFO_LARGE_RX = 0x120,
    USB_FIFO_CONTROL_TX = 0x10,
    USB_FIFO_LARGE_CONTROL_TX = 0x20,
    USB_FIFO_BULK_TX = 0x80,
    USB_FIFO_LARGE_BULK_TX = 0x100,
    USB_FIFO_PERIODIC_TX = 0x10,
    USB_FIFO_LARGE_SHARED_STORAGE = 0x220,
    USB_FIFO_LARGE_DEDICATED_STORAGE = 0x240,
    USB_FIFO_LARGE_AUDIO = 0x160,
    USB_FIFO_STORAGE_RX_MINIMUM = 0x90,
    USB_FIFO_AUDIO_RX_MINIMUM = 2 * ((USB_AUDIO_MAX_PACKET + 3) / 4 + 1) + 11
};

typedef struct UsbFifoLayout {
    bool fits;
    uint32_t rx;
    uint32_t nptx;
    uint32_t tx1;
    uint32_t tx2;
} UsbFifoLayout;

static inline uint32_t usb_fifo_region(uint32_t words, uint32_t start)
{
    return words << 16 | start;
}

static inline UsbFifoLayout usb_fifo_storage(uint32_t depth, bool dedicated)
{
    bool large =
        depth >= (dedicated ? USB_FIFO_LARGE_DEDICATED_STORAGE : USB_FIFO_LARGE_SHARED_STORAGE);
    uint32_t control = !dedicated ? 0 : large ? USB_FIFO_LARGE_CONTROL_TX : USB_FIFO_CONTROL_TX;
    uint32_t bulk = large ? USB_FIFO_LARGE_BULK_TX : USB_FIFO_BULK_TX;
    uint32_t transmit = control + bulk;
    if (depth < transmit + USB_FIFO_STORAGE_RX_MINIMUM) {
        return (UsbFifoLayout){.fits = false};
    }
    uint32_t rx = large ? USB_FIFO_LARGE_RX : depth - transmit;
    if (!dedicated) {
        return (UsbFifoLayout){true, rx, usb_fifo_region(bulk, rx), 0, 0};
    }
    return (UsbFifoLayout){true, rx, usb_fifo_region(control, rx),
                           usb_fifo_region(bulk, rx + control), 0};
}

static inline UsbFifoLayout usb_fifo_audio(uint32_t depth)
{
    bool large = depth >= USB_FIFO_LARGE_AUDIO;
    uint32_t control = large ? USB_FIFO_LARGE_CONTROL_TX : USB_FIFO_CONTROL_TX;
    uint32_t transmit = control + 2 * USB_FIFO_PERIODIC_TX;
    if (depth < transmit + USB_FIFO_AUDIO_RX_MINIMUM) {
        return (UsbFifoLayout){.fits = false};
    }
    uint32_t rx = large ? USB_FIFO_LARGE_RX : depth - transmit;
    uint32_t periodic = rx + control;
    return (UsbFifoLayout){true, rx, usb_fifo_region(control, rx),
                           usb_fifo_region(USB_FIFO_PERIODIC_TX, periodic),
                           usb_fifo_region(USB_FIFO_PERIODIC_TX, periodic + USB_FIFO_PERIODIC_TX)};
}
