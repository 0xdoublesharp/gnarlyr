#pragma once

#include <stdbool.h>
#include <stdint.h>

extern const char MUSIC_CHECK_PATH[];

enum {
    MUSIC_CHECK_PRINT_BYTES = 16,
    MUSIC_CHECK_VERSION_BYTES = 16,
    MUSIC_CHECK_RECORD_BYTES = MUSIC_CHECK_PRINT_BYTES + MUSIC_CHECK_VERSION_BYTES,
    MUSIC_CHECK_RECORD_MAGIC = 0x43535344
};

bool music_check_read_at(uint32_t offset, uint8_t *bytes, uint32_t length);
void music_check_put_version(uint8_t *bytes);
bool music_check_firmware_is_new(void);
void music_check_note_firmware(void);
uint32_t music_check_interrupted_scans(void);
bool music_check_covers_pending(void);
bool music_check_settled(void);
