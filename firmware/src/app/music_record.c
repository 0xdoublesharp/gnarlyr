#include "music_record.h"

#include "core/bytes.h"
#include "hal/storage.h"
#include "music_check.h"
#include "scan_guard.h"

enum {
    CARD_AT = 4,
    INTERNAL_AT = MUSIC_CHECK_PRINT_BYTES + MUSIC_CHECK_VERSION_BYTES,
    STATE_AT = MUSIC_CHECK_STATE_AT,
    GENERATION_AT = MUSIC_CHECK_GENERATION_AT
};

_Static_assert(STATE_AT == INTERNAL_AT + 12, "the state follows the internal fingerprint");
_Static_assert((int)MUSIC_DRIVE_UNKNOWN == (int)MUSIC_CHECK_STATE_UNKNOWN, "one unknown state");

static MusicFingerprint decode_print(const uint8_t *bytes)
{
    return (MusicFingerprint){.complete = true,
                              .first = bytes_le32(bytes),
                              .second = bytes_le32(bytes + 4),
                              .entries = bytes_le32(bytes + 8)};
}

static void encode_print(uint8_t *bytes, MusicFingerprint print)
{
    bytes_put_le32(bytes, print.first);
    bytes_put_le32(bytes + 4, print.second);
    bytes_put_le32(bytes + 8, print.entries);
}

bool music_record_read(MusicRecord *record)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    if (!music_check_read_at(0, bytes, sizeof bytes) ||
        bytes_le32(bytes) != MUSIC_CHECK_RECORD_MAGIC) {
        return false;
    }
    *record = (MusicRecord){.card = decode_print(bytes + CARD_AT),
                            .internal = decode_print(bytes + INTERNAL_AT),
                            .internal_state = bytes_le32(bytes + STATE_AT),
                            .generation = bytes_le32(bytes + GENERATION_AT)};
    return true;
}

void music_record_write(const MusicRecord *record)
{
    uint8_t bytes[MUSIC_CHECK_RECORD_BYTES];
    bytes_put_le32(bytes, MUSIC_CHECK_RECORD_MAGIC);
    encode_print(bytes + CARD_AT, record->card);
    music_check_put_version(bytes + MUSIC_CHECK_PRINT_BYTES);
    encode_print(bytes + INTERNAL_AT, record->internal);
    bytes_put_le32(bytes + STATE_AT, record->internal_state);
    bytes_put_le32(bytes + GENERATION_AT, record->generation);
    (void)hal_file_write_at(MUSIC_CHECK_PATH, 0, bytes, sizeof bytes);
}

static bool internal_matches(const MusicRecord *stored, const MusicRecord *current,
                             uint32_t internal_songs)
{
    if (stored->internal_state == MUSIC_DRIVE_UNKNOWN) {
        return current->internal_state == MUSIC_DRIVE_NONE ||
               (current->internal.complete && internal_songs == 0);
    }
    return stored->internal_state == current->internal_state &&
           (current->internal_state == MUSIC_DRIVE_NONE ||
            music_fingerprints_match(stored->internal, current->internal));
}

bool music_record_same_music(const MusicRecord *stored, const MusicRecord *current,
                             uint32_t internal_songs)
{
    return music_fingerprints_match(stored->card, current->card) &&
           internal_matches(stored, current, internal_songs);
}

bool music_record_settled(void)
{
    uint8_t bytes[SCAN_GUARD_ATTEMPTS_BYTE + 1];
    bool recorded = music_check_read_at(SCAN_GUARD_OFFSET, bytes, sizeof bytes) &&
                    bytes_le32(bytes) == SCAN_GUARD_MAGIC;
    return !recorded ||
           (bytes[SCAN_GUARD_RUNNING_BYTE] == 0 && bytes[SCAN_GUARD_ATTEMPTS_BYTE] <= 1);
}
