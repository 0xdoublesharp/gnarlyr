#include "extra_font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/drive_path.h"
#include "gfx/font_pack.h"
#include "hal/system.h"

static const char EXTRA_FONT_PATH[] =
    DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/extra.font";

static bool read_extra_font(void *context, uint32_t face, uint32_t offset, void *buffer,
                            uint32_t length)
{
    (void)face;
    const HalFile *file = context;
    return !hal_system_restart_pending() && hal_file_read(*file, offset, buffer, length);
}

void extra_font_open(HalFile *file)
{
    if (font_pack_extra_attached() || !hal_file_open(file, EXTRA_FONT_PATH)) {
        return;
    }
    if (!font_pack_attach_extra((FontPackReader){read_extra_font, file}, file->size)) {
        hal_file_close(file);
    }
}

void extra_font_close(HalFile *file)
{
    if (font_pack_extra_attached()) {
        font_pack_detach_extra();
        hal_file_close(file);
    }
}
