#include "music_record.h"

#include "core/bytes.h"
#include "hal/storage.h"
#include "music_check.h"

static void put_word(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static bool read_record(MusicFingerprint *print)
{
    uint8_t bytes[MUSIC_CHECK_PRINT_BYTES];
    if (!music_check_read_at(0, bytes, sizeof bytes) ||
        bytes_le32(bytes) != MUSIC_CHECK_RECORD_MAGIC) {
        return false;
    }
    *print = (MusicFingerprint){.complete = true,
                                .first = bytes_le32(bytes + 4),
                                .second = bytes_le32(bytes + 8),
                                .entries = bytes_le32(bytes + 12)};
    return true;
}

bool music_record_unchanged(MusicFingerprint current)
{
    MusicFingerprint stored;
    return read_record(&stored) && current.complete && stored.first == current.first &&
           stored.second == current.second && stored.entries == current.entries;
}

uint32_t music_record_entries(void)
{
    MusicFingerprint stored;
    return read_record(&stored) ? stored.entries : 0;
}

void music_record_remember(MusicFingerprint print)
{
    if (!print.complete) {
        return;
    }
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    put_word(bytes, MUSIC_CHECK_RECORD_MAGIC);
    put_word(bytes + 4, print.first);
    put_word(bytes + 8, print.second);
    put_word(bytes + 12, print.entries);
    music_check_put_version(bytes + MUSIC_CHECK_PRINT_BYTES);
    (void)hal_file_write_at(MUSIC_CHECK_PATH, 0, bytes, sizeof bytes);
}
