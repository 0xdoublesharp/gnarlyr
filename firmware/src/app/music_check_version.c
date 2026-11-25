#include "music_check.h"

#include <stddef.h>

#include "core/seam.h"

static bool version_matches(const uint8_t *bytes)
{
    uint8_t current[MUSIC_CHECK_VERSION_BYTES];
    music_check_put_version(current);
    for (size_t index = 0; index < MUSIC_CHECK_VERSION_BYTES; index++) {
        if (bytes[index] != current[index]) {
            return false;
        }
    }
    return true;
}

CORE_COLD bool music_check_firmware_is_new(void)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    return music_check_read_at(0, bytes, sizeof bytes) &&
           !version_matches(bytes + MUSIC_CHECK_PRINT_BYTES);
}
