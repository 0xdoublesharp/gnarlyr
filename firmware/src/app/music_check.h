#pragma once

#include <stdbool.h>
#include <stdint.h>

extern const char MUSIC_CHECK_PATH[];
extern const char MUSIC_GENERATION_PATH[];

enum {
    MUSIC_CHECK_PRINT_BYTES = 16,
    MUSIC_CHECK_VERSION_BYTES = 16,
    MUSIC_CHECK_RECORD_BYTES = MUSIC_CHECK_PRINT_BYTES + MUSIC_CHECK_VERSION_BYTES + 20,
    MUSIC_CHECK_RECORD_MAGIC = 0x32535344,
    MUSIC_CHECK_GENERATION_AT = MUSIC_CHECK_RECORD_BYTES - 4
};

bool music_check_read_at(uint32_t offset, uint8_t *bytes, uint32_t length);
void music_check_put_version(uint8_t *bytes);
bool music_check_firmware_is_new(void);
uint32_t music_check_interrupted_scans(void);
bool music_check_covers_pending(void);
uint32_t music_check_generation(void);
void music_check_bump_generation(void);
bool music_check_generation_moved(void);
