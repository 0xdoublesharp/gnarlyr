#include "music_check.h"

#include <stddef.h>
#include <stdint.h>

#include "core/bytes.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "scan_guard.h"

const char MUSIC_CHECK_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/scan.bin";

bool music_check_read_at(uint32_t offset, uint8_t *bytes, uint32_t length)
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

void music_check_put_version(uint8_t *bytes)
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

bool music_check_firmware_is_new(void)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    return read_bytes(bytes) && !version_matches(bytes + MUSIC_CHECK_PRINT_BYTES);
}

void music_check_note_firmware(void)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    if (read_bytes(bytes) && bytes_le32(bytes) == MUSIC_CHECK_RECORD_MAGIC &&
        !version_matches(bytes + MUSIC_CHECK_PRINT_BYTES)) {
        music_check_put_version(bytes + MUSIC_CHECK_PRINT_BYTES);
        (void)hal_file_write_at(MUSIC_CHECK_PATH, MUSIC_CHECK_PRINT_BYTES,
                                bytes + MUSIC_CHECK_PRINT_BYTES, MUSIC_CHECK_VERSION_BYTES);
    }
}

bool music_check_settled(void)
{
    uint8_t bytes[SCAN_GUARD_ATTEMPTS_BYTE + 1];
    bool recorded = music_check_read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
                    bytes_le32(bytes) == SCAN_GUARD_MAGIC;
    return !recorded ||
           (bytes[SCAN_GUARD_RUNNING_BYTE] == 0 && bytes[SCAN_GUARD_ATTEMPTS_BYTE] <= 1);
}

bool music_check_covers_pending(void)
{
    uint8_t bytes[SCAN_GUARD_COVERS_BYTE + 1];
    return music_check_read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
           bytes_le32(bytes) == SCAN_GUARD_MAGIC && bytes[SCAN_GUARD_COVERS_BYTE] != 0;
}

uint32_t music_check_interrupted_scans(void)
{
    uint8_t bytes[SCAN_GUARD_ATTEMPTS_BYTE + 1];
    bool running = music_check_read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
                   bytes_le32(bytes) == SCAN_GUARD_MAGIC && bytes[SCAN_GUARD_RUNNING_BYTE] != 0;
    return running ? bytes[SCAN_GUARD_ATTEMPTS_BYTE] : 0;
}
