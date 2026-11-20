#include "playback/resume_state.h"

#include "core/bytes.h"
#include "core/record.h"

enum {
    VERSION = 1,
    MAGIC_FIELD = 0,
    VERSION_FIELD = 4,
    SHUFFLE_FIELD = 6,
    REPEAT_FIELD = 7,
    FINGERPRINT_FIELD = 8,
    SEED_FIELD = 12,
    SOURCE_KIND_FIELD = 16,
    SOURCE_GROUP_FIELD = 17,
    SOURCE_SUBJECT_FIELD = 18,
    SOURCE_ARTIST_FIELD = 20,
    ANCHOR_FIELD = 22,
    CURSOR_POSITION_FIELD = 24,
    CURSOR_ALBUM_TRACK_FIELD = 26,
    TRACK_FIELD = 28,
    VOLUME_FIELD = 30,
    POSITION_FIELD = 32,
    FROM_UP_NEXT_FIELD = 36,
    CHECKSUM_FIELD = 40,
    MAGIC_SIZE = 4,
    BITS_PER_BYTE = 8
};

static const RecordKind KIND = {{'D', 'S', 'R', 'S'}, VERSION};

static void encode_plan(const PlayQueuePlan *plan, uint8_t *bytes)
{
    bytes[SHUFFLE_FIELD] = (uint8_t)plan->shuffle;
    bytes_put_le32(bytes + SEED_FIELD, plan->seed);
    bytes[SOURCE_KIND_FIELD] = (uint8_t)plan->source.kind;
    bytes[SOURCE_GROUP_FIELD] = (uint8_t)plan->source.group;
    bytes_put_le16(bytes + SOURCE_SUBJECT_FIELD, plan->source.subject);
    bytes_put_le16(bytes + SOURCE_ARTIST_FIELD, plan->source.artist);
    bytes_put_le16(bytes + ANCHOR_FIELD, plan->anchor_track);
}

void resume_state_encode(const ResumeState *state, uint8_t *bytes)
{
    for (int index = 0; index < RESUME_STATE_SIZE; index++) {
        bytes[index] = 0;
    }
    record_begin(bytes, &KIND);
    bytes[REPEAT_FIELD] = (uint8_t)state->repeat;
    bytes_put_le32(bytes + FINGERPRINT_FIELD, state->library_fingerprint);
    encode_plan(&state->plan, bytes);
    bytes_put_le16(bytes + CURSOR_POSITION_FIELD, state->cursor.position);
    bytes_put_le16(bytes + CURSOR_ALBUM_TRACK_FIELD, state->cursor.album_track);
    bytes_put_le16(bytes + TRACK_FIELD, state->track);
    bytes[VOLUME_FIELD] = state->volume;
    bytes_put_le32(bytes + POSITION_FIELD, state->position_ms);
    bytes[FROM_UP_NEXT_FIELD] = state->from_up_next ? 1 : 0;
    record_seal(bytes, CHECKSUM_FIELD);
}

static bool header_valid(const uint8_t *bytes)
{
    return record_is(bytes, &KIND) && record_sealed(bytes, CHECKSUM_FIELD);
}

static bool group_valid(uint8_t group)
{
    return (group >= LIBRARY_ARTISTS && group <= LIBRARY_GENRES) || group == LIBRARY_PLAYLISTS;
}

static bool modes_valid(const uint8_t *bytes)
{
    return bytes[SHUFFLE_FIELD] < SHUFFLE_MODE_COUNT && bytes[REPEAT_FIELD] < REPEAT_MODE_COUNT &&
           bytes[SOURCE_KIND_FIELD] <= BROWSE_SINGLE_TRACK &&
           group_valid(bytes[SOURCE_GROUP_FIELD]);
}

static PlayQueuePlan decode_plan(const uint8_t *bytes)
{
    return (PlayQueuePlan){
        .source =
            {
                .kind = (BrowseKind)bytes[SOURCE_KIND_FIELD],
                .group = (LibrarySection)bytes[SOURCE_GROUP_FIELD],
                .subject = (uint16_t)bytes_le16(bytes + SOURCE_SUBJECT_FIELD),
                .artist = (uint16_t)bytes_le16(bytes + SOURCE_ARTIST_FIELD),
            },
        .anchor_track = (uint16_t)bytes_le16(bytes + ANCHOR_FIELD),
        .shuffle = (ShuffleMode)bytes[SHUFFLE_FIELD],
        .seed = bytes_le32(bytes + SEED_FIELD),
    };
}

bool resume_state_decode(const uint8_t *bytes, uint32_t length, ResumeState *state)
{
    if (length < RESUME_STATE_SIZE || !header_valid(bytes) || !modes_valid(bytes)) {
        return false;
    }
    *state = (ResumeState){
        .library_fingerprint = bytes_le32(bytes + FINGERPRINT_FIELD),
        .plan = decode_plan(bytes),
        .repeat = (RepeatMode)bytes[REPEAT_FIELD],
        .volume = bytes[VOLUME_FIELD],
        .cursor = {(uint16_t)bytes_le16(bytes + CURSOR_POSITION_FIELD),
                   (uint16_t)bytes_le16(bytes + CURSOR_ALBUM_TRACK_FIELD)},
        .track = (uint16_t)bytes_le16(bytes + TRACK_FIELD),
        .position_ms = bytes_le32(bytes + POSITION_FIELD),
        .from_up_next = bytes[FROM_UP_NEXT_FIELD] != 0,
    };
    return true;
}
