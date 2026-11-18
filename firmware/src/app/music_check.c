#include "music_check.h"

#include <stddef.h>
#include <stdint.h>

#include "core/bytes.h"
#include "core/seam.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "scan_guard.h"

const char MUSIC_CHECK_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/scan.bin";
const char MUSIC_GENERATION_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/music_gen.bin";

CORE_COLD bool music_check_read_at(uint32_t offset, uint8_t *bytes, uint32_t length)
{
    HalFile file;
    if (!hal_file_open(&file, MUSIC_CHECK_PATH)) {
        return false;
    }
    bool read = file.size >= offset + length && hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return read;
}

static bool read_bytes(uint8_t *bytes)
{
    return music_check_read_at(0, bytes, MUSIC_CHECK_RECORD_BYTES);
}

CORE_COLD void music_check_put_version(uint8_t *bytes)
{
    const char *version = hal_system_version();
    for (size_t index = 0; index < MUSIC_CHECK_VERSION_BYTES; index++) {
        bytes[index] = 0;
    }
    for (size_t index = 0; index + 1 < MUSIC_CHECK_VERSION_BYTES && version[index] != '\0';
         index++) {
        bytes[index] = (uint8_t)version[index];
    }
}

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
    return read_bytes(bytes) && !version_matches(bytes + MUSIC_CHECK_PRINT_BYTES);
}

CORE_COLD bool music_check_covers_pending(void)
{
    uint8_t bytes[SCAN_GUARD_COVERS_BYTE + 1];
    return music_check_read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
           bytes_le32(bytes) == SCAN_GUARD_MAGIC && bytes[SCAN_GUARD_COVERS_BYTE] != 0;
}

CORE_COLD uint32_t music_check_interrupted_scans(void)
{
    uint8_t bytes[SCAN_GUARD_ATTEMPTS_BYTE + 1];
    bool running = music_check_read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
                   bytes_le32(bytes) == SCAN_GUARD_MAGIC && bytes[SCAN_GUARD_RUNNING_BYTE] != 0;
    return running ? bytes[SCAN_GUARD_ATTEMPTS_BYTE] : 0;
}

CORE_COLD uint32_t music_check_generation(void)
{
    uint8_t bytes[4];
    return hal_internal_file_read(MUSIC_GENERATION_PATH, bytes, sizeof bytes) ? bytes_le32(bytes)
                                                                              : 0;
}

CORE_COLD void music_check_bump_generation(void)
{
    uint32_t next = music_check_generation() + 1u;
    uint8_t bytes[4] = {(uint8_t)next, (uint8_t)(next >> 8), (uint8_t)(next >> 16),
                        (uint8_t)(next >> 24)};
    (void)hal_internal_file_write_at(MUSIC_GENERATION_PATH, 0, bytes, sizeof bytes);
}

CORE_COLD bool music_check_generation_moved(void)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    return read_bytes(bytes) && bytes_le32(bytes) == MUSIC_CHECK_RECORD_MAGIC &&
           (bytes_le32(bytes + MUSIC_CHECK_GENERATION_AT) != music_check_generation() ||
            bytes_le32(bytes + MUSIC_CHECK_STATE_AT) == MUSIC_CHECK_STATE_UNKNOWN);
}
