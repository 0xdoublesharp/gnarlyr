#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emmc.h"
#include "hal/system.h"

enum {
    SECTOR_WORDS = 128,
    TAG_OFFSET = 0x30,
    TAG_LENGTH = 8
};

static const char VERSION_TEXT[] = FIRMWARE_VERSION;
static char version[sizeof VERSION_TEXT + 1 + TAG_LENGTH];
static bool tried;

static bool is_printable(uint8_t value)
{
    return value >= 0x20 && value < 0x7F;
}

static void read_tag(void)
{
    static uint32_t header[SECTOR_WORDS];
    EmmcInfo info;
    if (!emmc_start(&info) || !emmc_read(NULL, info.layout.slot_1, 1, header)) {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)header + TAG_OFFSET;
    size_t length = sizeof VERSION_TEXT - 1;
    if (!is_printable(bytes[0])) {
        return;
    }
    version[length++] = '-';
    for (int index = 0; index < TAG_LENGTH && is_printable(bytes[index]); index++) {
        version[length++] = (char)bytes[index];
    }
}

const char *hal_system_version(void)
{
    if (!tried) {
        tried = true;
        for (size_t index = 0; index < sizeof VERSION_TEXT; index++) {
            version[index] = VERSION_TEXT[index];
        }
        read_tag();
    }
    return version;
}
