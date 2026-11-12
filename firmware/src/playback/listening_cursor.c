#include "playback/listening_log.h"

#include "core/hash.h"

#include "listening_record.h"

static bool record_kinds_valid(const uint8_t *bytes)
{
    return listening_kinds_valid(bytes[RECORD_ENDED], bytes[RECORD_OUTPUT], bytes[RECORD_FORMAT]) &&
           (bytes[RECORD_CLOCK_FLAGS] & ~CLOCK_UTC) == 0;
}

static void decode_play(const uint8_t *bytes, ListeningPlay *play)
{
    play->track = (ListeningTrack){.key = store_get32(bytes + RECORD_TRACK),
                                   .album = store_get32(bytes + RECORD_ALBUM),
                                   .artist = store_get32(bytes + RECORD_ARTIST),
                                   .index = store_get16(bytes + RECORD_INDEX)};
    play->length_ms = store_get32(bytes + RECORD_LENGTH);
    play->listened_ms = store_get32(bytes + RECORD_LISTENED);
    play->start_ms = store_get32(bytes + RECORD_START);
    play->end_ms = store_get32(bytes + RECORD_END);
    play->uptime_s = store_get32(bytes + RECORD_UPTIME);
    bool timed = (bytes[RECORD_CLOCK_FLAGS] & CLOCK_UTC) != 0;
    play->clock_s = timed ? store_get32(bytes + RECORD_CLOCK) : 0;
    play->offset_minutes = timed ? (int16_t)store_get16(bytes + RECORD_OFFSET) : 0;
    play->end = (ListeningEnd)bytes[RECORD_ENDED];
    play->output = (ListeningOutput)bytes[RECORD_OUTPUT];
    play->format = (ListeningFormat)bytes[RECORD_FORMAT];
}

static bool decode_record(const uint8_t *bytes, ListeningRecord *record)
{
    if (store_get32(bytes + RECORD_CHECK) != hash_update(HASH_START, bytes, RECORD_CHECK) ||
        !record_kinds_valid(bytes)) {
        return false;
    }
    record->sequence = store_get32(bytes + RECORD_SEQUENCE);
    record->boot = store_get32(bytes + RECORD_BOOT);
    decode_play(bytes, &record->play);
    return true;
}

bool listening_play_listened(const ListeningPlay *play)
{
    if (play->length_ms == 0) {
        return play->end == LISTENING_FINISHED;
    }
    return play->listened_ms >= play->length_ms - play->length_ms / 2;
}

void listening_cursor_begin(ListeningCursor *cursor, const ListeningLog *log, uint8_t run)
{
    *cursor = (ListeningCursor){
        .file = &log->file, .next = log->root.next, .kept = log->root.kept, .run = run};
}

void listening_cursor_rewind(ListeningCursor *cursor)
{
    cursor->position = 0;
    cursor->damaged = 0;
}

static uint32_t sequence_at(const ListeningCursor *cursor, uint16_t position)
{
    return cursor->next - cursor->kept + position;
}

static uint16_t run_length(const ListeningCursor *cursor)
{
    uint32_t slot = sequence_at(cursor, cursor->position) % LISTENING_LOG_CAPACITY;
    uint16_t count = (uint16_t)(LISTENING_SECTOR_RECORDS - slot % LISTENING_SECTOR_RECORDS);
    uint16_t left = (uint16_t)(cursor->kept - cursor->position);
    count = count < cursor->run ? count : cursor->run;
    return count < left ? count : left;
}

StoreResult listening_cursor_step(ListeningCursor *cursor, ListeningVisitor visitor)
{
    uint8_t block[STORE_BLOCK_BYTES];
    ListeningRecord record;
    if (cursor->position >= cursor->kept) {
        return STORE_DONE;
    }
    uint32_t sequence = sequence_at(cursor, cursor->position);
    uint16_t count = run_length(cursor);
    if (count == 0 || !store_file_read(cursor->file, listening_record_offset(sequence), block,
                                       (uint32_t)count * LISTENING_RECORD_BYTES)) {
        return STORE_FAILED;
    }
    for (uint16_t index = 0; index < count; index++) {
        if (decode_record(block + index * LISTENING_RECORD_BYTES, &record) &&
            record.sequence == sequence + index) {
            visitor.visit(visitor.context, &record);
        } else {
            cursor->damaged++;
        }
    }
    cursor->position = (uint16_t)(cursor->position + count);
    return cursor->position >= cursor->kept ? STORE_DONE : STORE_WORKING;
}

bool listening_cursor_fetch(const ListeningCursor *cursor, uint16_t position,
                            ListeningRecord *record)
{
    uint8_t bytes[LISTENING_RECORD_BYTES];
    uint32_t sequence = sequence_at(cursor, position);
    return position < cursor->kept &&
           store_file_read(cursor->file, listening_record_offset(sequence), bytes, sizeof bytes) &&
           decode_record(bytes, record) && record->sequence == sequence;
}
