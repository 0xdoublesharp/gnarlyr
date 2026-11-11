#pragma once

#include <stdbool.h>
#include <stdint.h>

#define OVERLAY_WINDOW_BASE 0x0307A000u
#define OVERLAY_WINDOW_BYTES 0x18000u
#define OVERLAY_HEADER_BYTES 16u
#define RESCAN_OVERLAY_IMAGE_OFFSET 0x200000u
#define RESCAN_OVERLAY_MAGIC 0x4C564F45u
#define USB_OVERLAY_IMAGE_OFFSET 0x43C000u
#define USB_OVERLAY_MAGIC 0x42535545u
#define MODULE_SLOT_BASE 0x03084400u
#define MODULE_SLOT_BYTES 0xAC00u
#define MODULE_TABLE_OFFSET 0x218000u
#define MODULE_TABLE_MAGIC 0x544D5344u
#define MODULE_TABLE_ROWS 31u
#define MODULE_MAGIC 0x004D5344u
#define MODULE_MAGIC_SHIFT 24
#define EXTENSION_MODULE_BASE 0x0304A000u
#define EXTENSION_MODULE_BYTES 0x2000u

typedef struct BoardOverlay {
    uint32_t image_offset;
    uint32_t magic;
    uint8_t *base;
    uint32_t capacity;
    uint32_t module;
} BoardOverlay;

bool board_overlay_load(const BoardOverlay *overlay);
const void *board_usb_overlay_load(void);
void board_module_forget(void);
