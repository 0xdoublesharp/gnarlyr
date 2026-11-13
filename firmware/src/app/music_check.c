#include "music_check.h"

#include <stddef.h>
#include <stdint.h>

#include "core/bytes.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "scan_guard.h"

const char MUSIC_CHECK_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/scan.bin";

enum {
    PRINT_BYTES = 16,
    VERSION_BYTES = 16,
    RECORD_BYTES = PRINT_BYTES + VERSION_BYTES,
    RECORD_MAGIC = 0x43535344
};

static void put_word(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static bool read_at(uint32_t offset, uint8_t *bytes, uint32_t length)
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
    return read_at(0, bytes, RECORD_BYTES);
}

static bool decode_print(const uint8_t *bytes, MusicFingerprint *print)
{
    if (bytes_le32(bytes) != RECORD_MAGIC) {
        return false;
    }
    *print = (MusicFingerprint){.complete = true,
                                .first = bytes_le32(bytes + 4),
                                .second = bytes_le32(bytes + 8),
                                .entries = bytes_le32(bytes + 12)};
    return true;
}

static bool read_record(MusicFingerprint *print)
{
    uint8_t bytes[RECORD_BYTES];
    return read_bytes(bytes) && decode_print(bytes, print);
}

bool music_check_unchanged(MusicFingerprint current)
{
    MusicFingerprint stored;
    return read_record(&stored) && stored.complete && current.complete &&
           stored.first == current.first && stored.second == current.second &&
           stored.entries == current.entries;
}

uint32_t music_check_entries(void)
{
    MusicFingerprint stored;
    return read_record(&stored) && stored.complete ? stored.entries : 0;
}

static void put_version(uint8_t *bytes)
{
    const char *version = hal_system_version();
    for (size_t index = 0; index < VERSION_BYTES; index++) {
        bytes[index] = 0;
    }
    for (size_t index = 0; index + 1 < VERSION_BYTES && version[index] != '\0'; index++) {
        bytes[index] = (uint8_t)version[index];
    }
}

static bool version_matches(const uint8_t *bytes)
{
    uint8_t current[VERSION_BYTES];
    put_version(current);
    for (size_t index = 0; index < VERSION_BYTES; index++) {
        if (bytes[index] != current[index]) {
            return false;
        }
    }
    return true;
}

static void write_record(MusicFingerprint print)
{
    uint8_t bytes[RECORD_BYTES];
    put_word(bytes, RECORD_MAGIC);
    put_word(bytes + 4, print.first);
    put_word(bytes + 8, print.second);
    put_word(bytes + 12, print.entries);
    put_version(bytes + PRINT_BYTES);
    (void)hal_file_write_at(MUSIC_CHECK_PATH, 0, bytes, sizeof bytes);
}

void music_check_remember(MusicFingerprint print)
{
    if (print.complete) {
        write_record(print);
    }
}

bool music_check_firmware_is_new(void)
{
    uint8_t bytes[RECORD_BYTES];
    return read_bytes(bytes) && !version_matches(bytes + PRINT_BYTES);
}

void music_check_note_firmware(void)
{
    uint8_t bytes[RECORD_BYTES];
    MusicFingerprint stored;
    if (read_bytes(bytes) && decode_print(bytes, &stored) &&
        !version_matches(bytes + PRINT_BYTES)) {
        write_record(stored);
    }
}

bool music_check_settled(void)
{
    uint8_t bytes[SCAN_GUARD_ATTEMPTS_BYTE + 1];
    bool recorded =
        read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) && bytes_le32(bytes) == SCAN_GUARD_MAGIC;
    return !recorded ||
           (bytes[SCAN_GUARD_RUNNING_BYTE] == 0 && bytes[SCAN_GUARD_ATTEMPTS_BYTE] <= 1);
}

bool music_check_covers_pending(void)
{
    uint8_t bytes[SCAN_GUARD_COVERS_BYTE + 1];
    return read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
           bytes_le32(bytes) == SCAN_GUARD_MAGIC && bytes[SCAN_GUARD_COVERS_BYTE] != 0;
}

uint32_t music_check_interrupted_scans(void)
{
    uint8_t bytes[SCAN_GUARD_ATTEMPTS_BYTE + 1];
    bool running = read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
                   bytes_le32(bytes) == SCAN_GUARD_MAGIC && bytes[SCAN_GUARD_RUNNING_BYTE] != 0;
    return running ? bytes[SCAN_GUARD_ATTEMPTS_BYTE] : 0;
}
