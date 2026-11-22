#include "extra_font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/drive_path.h"
#include "core/i18n.h"
#include "core/utf8.h"
#include "gfx/font_pack.h"
#include "hal/system.h"

static const char EXTRA_FONT_PATH[] =
    DRIVE_PATH_INTERNAL_ROOT "/" FIRMWARE_DATA_DIRECTORY "/fonts/extra.font";

static const char USB_FIGURES[] = "0123456789.,:%/+-";

static const uint16_t USB_STATE_TEXT[] = {
    STR_PLUG_COMPUTER,     STR_POWER_CONNECTING,  STR_USB_STATE_CONNECTED,
    STR_USB_STATE_READING, STR_USB_STATE_WRITING, STR_USB_STATE_WAITING,
    STR_USB_STATE_LEAVING, STR_USB_UNPLUGGED,     STR_USB_STOPPED,
};

static const uint16_t USB_SMALL_TEXT[] = {
    STR_USB,
    STR_PLUG_COMPUTER_LINE,
    STR_USB_EJECT_DRIVE,
    STR_USB_EJECT_DRIVES,
    STR_USB_DRIVE_HINT,
    STR_USB_DRIVES_HINT,
    STR_POWER_INTERNAL,
    STR_USB_CARD,
    STR_USB_COPIED,
    STR_USB_GB,
    STR_USB_MB,
    STR_USB_RATE_UNIT,
    STR_USB_TOTALS,
    STR_USB_FREE,
    STR_USB_TOTAL,
    STR_USB_EJECTED,
    STR_USB_NO_CARD,
    STR_USB_STAT_INTERNAL,
    STR_USB_STAT_CARD,
    STR_USB_STAT_READ,
    STR_USB_STAT_WRITTEN,
    STR_USB_HIGH_SPEED,
    STR_USB_FULL_SPEED,
    STR_POWER_TITLE,
    STR_POWER_PERCENT,
    STR_USB_MENU_DETAILS,
    STR_USB_MENU_MORE,
    STR_USB_MENU_OVERVIEW,
    STR_USB_HOLD_BACK_HINT,
    STR_USB_KEEP_HOLDING,
    STR_USB_LEAVING_HINT,
    STR_USB_WAITING_HINT,
    STR_USB_UNPLUGGED_HINT,
    STR_USB_STOPPED_HINT,
    STR_UPDATE,
    STR_UPDATE_CHECKING,
    STR_UPDATE_INSTALLING,
    STR_UPDATE_PROGRESS,
    STR_UPDATE_INSTALLED,
    STR_UPDATE_UNPLUG,
    STR_UPDATE_UNPLUG_FIRST,
    STR_UPDATE_TURNING_OFF,
    STR_UPDATE_FAILED,
    STR_UPDATE_FAILED_HINT,
    STR_UPDATE_NOT_INSTALLED,
    STR_UPDATE_REFUSED,
    STR_UPDATE_INSTALLING_STOCK,
    STR_UPDATE_INSTALLED_STOCK,
    STR_USB_STAT_COMMANDS,
    STR_USB_STAT_PER_COMMAND,
    STR_USB_STAT_FREE,
    STR_USB_STAT_PEAK,
    STR_USB_STAT_AVERAGE,
    STR_USB_STAT_BUSY,
    STR_USB_STAT_ERRORS,
    STR_USB_STAT_LINK,
    STR_USB_DAC_TIME,
    STR_USB_KB,
    STR_USB_MB_S,
    STR_USB_KB_S,
    STR_USB_ERRORS_VALUE,
    STR_USB_LINK_HIGH,
    STR_USB_LINK_FULL,
    STR_USB_UNKNOWN,
};

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

static void hold_codepoints(const Font *font, const char *text)
{
    for (uint32_t codepoint = utf8_next(&text); codepoint != 0; codepoint = utf8_next(&text)) {
        (void)font_find_glyph(font, codepoint);
    }
}

static void hold_text(const Font *font, const uint16_t *ids, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        hold_codepoints(font, i18n_text((StringId)ids[index]));
    }
}

void extra_font_hold_for_usb(const Theme *theme)
{
    font_pack_hold(FONT_PACK_HOLD_PIN);
    hold_codepoints(theme->fonts.title, USB_FIGURES);
    hold_codepoints(theme->fonts.small, USB_FIGURES);
    uint32_t states = sizeof USB_STATE_TEXT / sizeof USB_STATE_TEXT[0];
    hold_text(theme->fonts.title, USB_STATE_TEXT, states);
    hold_text(theme->fonts.small, USB_STATE_TEXT, states);
    hold_text(theme->fonts.small, USB_SMALL_TEXT, sizeof USB_SMALL_TEXT / sizeof USB_SMALL_TEXT[0]);
    font_pack_hold(FONT_PACK_HOLD_FREEZE);
}
