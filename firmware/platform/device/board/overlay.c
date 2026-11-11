#include "overlay.h"

#include <stddef.h>
#include <stdint.h>

#include "app/app.h"
#include "audio_process.h"
#include "board.h"
#include "core/seam.h"
#include "device/audio_process.h"
#include "device/image_crc.h"
#include "emmc.h"
#include "hal/audio.h"
#include "hal/system.h"

#define CODE_BASE 0x03050000u

enum {
    SECTOR_BYTES = 512,
    SECTORS_PER_READ = 8
};

_Static_assert(OVERLAY_WINDOW_BYTES % SECTOR_BYTES == 0, "whole sectors fit the window");
_Static_assert(MODULE_SLOT_BYTES % SECTOR_BYTES == 0, "whole sectors fit the module slot");
_Static_assert(MODULE_SLOT_BASE + MODULE_SLOT_BYTES == AUDIO_PROCESS_BASE,
               "the UI slot ends where audio processing starts");
_Static_assert(AUDIO_PROCESS_BASE + AUDIO_PROCESS_BYTES == GLYPH_WINDOW_BASE,
               "audio processing ends where the glyph cache starts");

static const BoardImageTable MODULE_TABLE = {MODULE_TABLE_OFFSET, MODULE_TABLE_MAGIC,
                                             MODULE_TABLE_ROWS};
static uint8_t loaded_module;

extern const uint8_t device_code_end[];

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint32_t running_code_crc(void)
{
    static uint32_t crc;
    static bool known;
    if (!known) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)CODE_BASE;
        crc = image_crc_update(0, code, (uint32_t)((uintptr_t)device_code_end - CODE_BASE));
        known = true;
    }
    return crc;
}

static bool read_rest(const BoardOverlay *overlay, uint32_t first_sector, uint32_t sectors)
{
    uint8_t *target = overlay->base + SECTOR_BYTES;
    for (uint32_t done = 0; done < sectors; done += SECTORS_PER_READ) {
        uint32_t count = sectors - done < SECTORS_PER_READ ? sectors - done : SECTORS_PER_READ;
        if (overlay->module != 0 && (uintptr_t)overlay->base == MODULE_SLOT_BASE) {
            hal_audio_service();
        }
        if (!emmc_read(NULL, first_sector + done, count, target + done * SECTOR_BYTES)) {
            return false;
        }
    }
    return true;
}

static uint32_t header_size(const BoardOverlay *overlay)
{
    const uint8_t *base = overlay->base;
    uint32_t size = le32(base + 4);
    if (le32(base) != overlay->magic || size < OVERLAY_HEADER_BYTES || size > overlay->capacity ||
        le32(base + 12) != running_code_crc()) {
        return 0;
    }
    return size;
}

static bool body_intact(const BoardOverlay *overlay, uint32_t size)
{
    const uint8_t *base = overlay->base;
    return image_crc_update(0, base + OVERLAY_HEADER_BYTES, size - OVERLAY_HEADER_BYTES) ==
           le32(base + 8);
}

static uint32_t already_loaded(const BoardOverlay *overlay)
{
    uint32_t size = header_size(overlay);
    return size != 0 && body_intact(overlay, size) ? size : 0;
}

static uint32_t image_offset(uint32_t slot, const BoardOverlay *overlay)
{
    uint32_t row[2];
    if (overlay->module == 0) {
        return overlay->image_offset;
    }
    bool listed = board_slot_table_row(slot, &MODULE_TABLE, overlay->module - 1, row);
    return listed && row[0] % SECTOR_BYTES == 0 ? row[0] : 0;
}

static uint32_t load_from(uint32_t slot, const BoardOverlay *overlay)
{
    uint32_t offset = image_offset(slot, overlay);
    uint32_t first_sector = slot + offset / SECTOR_BYTES;
    if (offset == 0 || !emmc_read(NULL, first_sector, 1, overlay->base)) {
        return 0;
    }
    uint32_t size = header_size(overlay);
    uint32_t sectors = (size + SECTOR_BYTES - 1) / SECTOR_BYTES;
    if (size == 0 || !read_rest(overlay, first_sector + 1, sectors - 1) ||
        !body_intact(overlay, size)) {
        return 0;
    }
    return size;
}

static uint32_t load_from_storage(const BoardOverlay *overlay)
{
    EmmcInfo info;
    if (!emmc_start(&info)) {
        return 0;
    }
    uint32_t size = load_from(info.layout.slot_1, overlay);
    return size != 0 ? size : load_from(info.layout.slot_2, overlay);
}

static void clear(const BoardOverlay *overlay, uint32_t from)
{
    for (uint32_t index = from; index < overlay->capacity; index++) {
        overlay->base[index] = 0;
    }
}

bool board_overlay_load(const BoardOverlay *overlay)
{
    uint32_t size = already_loaded(overlay);
    if (size == 0) {
        size = load_from_storage(overlay);
    }
    clear(overlay, size);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    return size != 0;
}

static const void *load_window(uint32_t offset, uint32_t magic)
{
    uint8_t *base = (uint8_t *)(uintptr_t)OVERLAY_WINDOW_BASE;
    BoardOverlay overlay = {offset, magic, base, OVERLAY_WINDOW_BYTES, 0};
    if (board_audio_workspace_lent()) {
        return NULL;
    }
    board_audio_shut_down();
    board_module_forget();
    return board_overlay_load(&overlay) ? base + OVERLAY_HEADER_BYTES : NULL;
}

const void *hal_system_load_rescan_code(void)
{
    return load_window(RESCAN_OVERLAY_IMAGE_OFFSET, RESCAN_OVERLAY_MAGIC);
}

const void *board_usb_overlay_load(void)
{
    return load_window(USB_OVERLAY_IMAGE_OFFSET, USB_OVERLAY_MAGIC);
}

void board_module_forget(void)
{
    app_set_usb_renderer(NULL);
    loaded_module = 0;
    board_audio_process_forget();
}

static bool load_module(uint32_t module, uint8_t *base)
{
    BoardOverlay overlay = {0, MODULE_MAGIC | module << MODULE_MAGIC_SHIFT, base, MODULE_SLOT_BYTES,
                            module};
    loaded_module = 0;
    hal_system_coast(false);
    return board_overlay_load(&overlay);
}

CORE_SEAM const void *hal_system_load_module(uint32_t module)
{
    uint8_t *base = (uint8_t *)(uintptr_t)MODULE_SLOT_BASE;
    if (module == 0 || module > MODULE_TABLE_ROWS) {
        return NULL;
    }
    if (module != loaded_module) {
        if (!load_module(module, base)) {
            return NULL;
        }
        loaded_module = (uint8_t)module;
    }
    return base + OVERLAY_HEADER_BYTES;
}
