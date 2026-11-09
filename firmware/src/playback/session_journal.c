#include "playback/session_journal.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    MAGIC = 0x4E4A5345,
    VERSION = 1,
    HEADER_BYTES = 16,
    PAYLOAD_BYTES = RESUME_STATE_SIZE + UP_NEXT_STATE_SIZE + 8,
    RECORD_BYTES_USED = HEADER_BYTES + PAYLOAD_BYTES,
    ORDER_FIELD = RESUME_STATE_SIZE + UP_NEXT_STATE_SIZE,
    KIND_FIELD = ORDER_FIELD + 4
};

typedef struct Record {
    SessionLoad result;
    uint32_t generation;
    SessionState state;
} Record;

static uint16_t get16(const uint8_t *bytes, uint32_t offset)
{
    return (uint16_t)(bytes[offset] | bytes[offset + 1] << 8);
}

static uint32_t get32(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t)get16(bytes, offset) | (uint32_t)get16(bytes, offset + 2) << 16;
}

static void put16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    put16(bytes, offset, (uint16_t)value);
    put16(bytes, offset + 2, (uint16_t)(value >> 16));
}

static bool config_valid(const SessionJournalConfig *config)
{
    return config->source.read != NULL && config->size >= SESSION_FILE_BYTES &&
           config->track_limit <= UINT16_MAX;
}

static uint32_t checksum(const uint8_t *bytes)
{
    uint32_t hash = hash_update(HASH_START, bytes, 12);
    return hash_update(hash, bytes + HEADER_BYTES, get16(bytes, 6));
}

static bool header_valid(const uint8_t *bytes)
{
    return get32(bytes, 0) == MAGIC && get16(bytes, 6) <= SESSION_RECORD_BYTES - HEADER_BYTES &&
           get32(bytes, 12) == checksum(bytes);
}

static bool ids_valid(const SessionJournalConfig *config, const SessionState *state)
{
    if (state->resume.track >= config->track_limit || state->up_next.count > UP_NEXT_CAPACITY) {
        return false;
    }
    for (uint16_t index = 0; index < state->up_next.count; index++) {
        if (state->up_next.tracks[index] >= config->track_limit) {
            return false;
        }
    }
    return true;
}

static bool kind_valid(const SessionState *state)
{
    switch (state->queue_kind) {
    case SESSION_QUEUE_GENERATED:
        return state->order_generation == 0;
    case SESSION_QUEUE_STORED:
        return !state->resume.from_up_next && state->resume.cursor.album_track == 0 &&
               state->up_next.count == 0;
    case SESSION_QUEUE_EMPTY:
        return state->resume.from_up_next && state->order_generation == 0;
    }
    return false;
}

static bool decode_payload(const SessionJournalConfig *config, const uint8_t *bytes,
                           SessionState *state)
{
    *state = (SessionState){.order_generation = get32(bytes, ORDER_FIELD),
                            .queue_kind = (SessionQueueKind)bytes[KIND_FIELD]};
    return resume_state_decode(bytes, RESUME_STATE_SIZE, &state->resume) &&
           state->resume.library_fingerprint == config->fingerprint &&
           up_next_decode(bytes + RESUME_STATE_SIZE, UP_NEXT_STATE_SIZE, config->fingerprint,
                          &state->up_next) &&
           ids_valid(config, state) && kind_valid(state);
}

static bool blank_record(const uint8_t *bytes)
{
    for (int index = 0; index < SESSION_RECORD_BYTES; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static Record read_record(const SessionJournalConfig *config, uint8_t bank)
{
    uint8_t bytes[SESSION_RECORD_BYTES];
    if (!config->source.read(config->source.context, (uint32_t)bank * SESSION_RECORD_BYTES, bytes,
                             sizeof bytes)) {
        return (Record){.result = SESSION_UNREADABLE};
    }
    if (!header_valid(bytes)) {
        return (Record){.result = blank_record(bytes) ? SESSION_EMPTY : SESSION_CORRUPT};
    }
    if (get16(bytes, 4) != VERSION) {
        return (Record){.result = SESSION_UNSUPPORTED};
    }
    if (get16(bytes, 6) == PAYLOAD_BYTES && get32(bytes, HEADER_BYTES + 8) != config->fingerprint) {
        return (Record){.result = SESSION_EMPTY};
    }
    Record record = {.generation = get32(bytes, 8)};
    record.result = get16(bytes, 6) == PAYLOAD_BYTES &&
                            decode_payload(config, bytes + HEADER_BYTES, &record.state)
                        ? SESSION_READY
                        : SESSION_CORRUPT;
    return record;
}

static bool newer(uint32_t candidate, uint32_t previous)
{
    uint32_t distance = candidate - previous;
    return distance != 0 && distance < UINT32_C(0x80000000);
}

static void select_record(SessionJournal *journal, uint8_t bank, const Record *record,
                          SessionState *state)
{
    journal->generation = record->generation;
    journal->bank = bank;
    journal->valid = true;
    *state = record->state;
}

SessionLoad session_journal_open(SessionJournal *journal, SessionJournalConfig config,
                                 SessionState *state)
{
    *journal = (SessionJournal){.config = config};
    if (!config_valid(&config)) {
        journal->needs_reload = true;
        return SESSION_UNREADABLE;
    }
    Record records[2] = {read_record(&config, 0), read_record(&config, 1)};
    for (int index = 0; index < 2; index++) {
        if (records[index].result == SESSION_UNREADABLE ||
            records[index].result == SESSION_UNSUPPORTED) {
            journal->needs_reload = true;
            return records[index].result;
        }
    }
    if (records[0].result != SESSION_READY && records[1].result != SESSION_READY) {
        return records[0].result == SESSION_CORRUPT || records[1].result == SESSION_CORRUPT
                   ? SESSION_CORRUPT
                   : SESSION_EMPTY;
    }
    uint8_t bank =
        records[1].result == SESSION_READY && (records[0].result != SESSION_READY ||
                                               newer(records[1].generation, records[0].generation))
            ? 1
            : 0;
    select_record(journal, bank, &records[bank], state);
    return SESSION_READY;
}

SessionLoad session_journal_previous(SessionJournal *journal, SessionState *state)
{
    if (!journal->valid) {
        return SESSION_EMPTY;
    }
    uint8_t bank = (uint8_t)(1 - journal->bank);
    Record record = read_record(&journal->config, bank);
    if (record.result == SESSION_UNREADABLE || record.result == SESSION_UNSUPPORTED) {
        journal->needs_reload = true;
        return record.result;
    }
    if (record.result != SESSION_READY || !newer(journal->generation, record.generation)) {
        return SESSION_EMPTY;
    }
    select_record(journal, bank, &record, state);
    return SESSION_READY;
}

static void make_record(const SessionJournal *journal, const SessionState *state, uint8_t *bytes)
{
    for (int index = 0; index < SESSION_RECORD_BYTES; index++) {
        bytes[index] = 0;
    }
    put32(bytes, 0, MAGIC);
    put16(bytes, 4, VERSION);
    put16(bytes, 6, PAYLOAD_BYTES);
    put32(bytes, 8, journal->valid ? journal->generation + 1 : 1);
    ResumeState resume = state->resume;
    if (state->queue_kind == SESSION_QUEUE_EMPTY) {
        resume.plan.source.group = LIBRARY_ARTISTS;
    }
    resume_state_encode(&resume, bytes + HEADER_BYTES);
    up_next_encode(&state->up_next, journal->config.fingerprint,
                   bytes + HEADER_BYTES + RESUME_STATE_SIZE);
    put32(bytes, HEADER_BYTES + ORDER_FIELD, state->order_generation);
    bytes[HEADER_BYTES + KIND_FIELD] = (uint8_t)state->queue_kind;
    put32(bytes, 12, checksum(bytes));
}

static bool same_record(const uint8_t *first, const uint8_t *second)
{
    for (int index = 0; index < RECORD_BYTES_USED; index++) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

SessionSave session_journal_save(SessionJournal *journal, const SessionState *state)
{
    if (journal->needs_reload || !config_valid(&journal->config) || journal->config.write == NULL ||
        !ids_valid(&journal->config, state) || !kind_valid(state)) {
        return SESSION_SAVE_FAILED;
    }
    uint8_t bytes[SESSION_RECORD_BYTES];
    uint8_t readback[SESSION_RECORD_BYTES];
    SessionState check;
    make_record(journal, state, bytes);
    if (!decode_payload(&journal->config, bytes + HEADER_BYTES, &check)) {
        return SESSION_SAVE_FAILED;
    }
    uint8_t bank = journal->valid ? (uint8_t)(1 - journal->bank) : 0;
    uint32_t offset = (uint32_t)bank * SESSION_RECORD_BYTES;
    (void)journal->config.write(journal->config.source.context, offset, bytes, sizeof bytes);
    if (!journal->config.source.read(journal->config.source.context, offset, readback,
                                     sizeof readback)) {
        journal->needs_reload = true;
        return SESSION_SAVE_UNCERTAIN;
    }
    if (!same_record(bytes, readback)) {
        journal->needs_reload |=
            header_valid(readback) && newer(get32(readback, 8), journal->generation);
        return journal->needs_reload ? SESSION_SAVE_UNCERTAIN : SESSION_SAVE_FAILED;
    }
    journal->generation = get32(bytes, 8);
    journal->bank = bank;
    journal->valid = true;
    return SESSION_SAVED;
}
