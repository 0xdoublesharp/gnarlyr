#include "music_check.h"

#include "core/bytes.h"
#include "core/seam.h"
#include "scan_guard.h"

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

CORE_COLD bool music_check_generation_moved(void)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    return music_check_read_at(0, bytes, sizeof bytes) &&
           bytes_le32(bytes) == MUSIC_CHECK_RECORD_MAGIC &&
           (bytes_le32(bytes + MUSIC_CHECK_GENERATION_AT) != music_check_generation() ||
            bytes_le32(bytes + MUSIC_CHECK_STATE_AT) == MUSIC_CHECK_STATE_UNKNOWN);
}
