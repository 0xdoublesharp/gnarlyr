#include "playback/session_journal.h"

#include <stddef.h>

enum {
    ORDER_FIELD = RESUME_STATE_SIZE + UP_NEXT_STATE_SIZE,
    KIND_FIELD = ORDER_FIELD + 4,
    LEGACY_PAYLOAD_BYTES = ORDER_FIELD + 8,
    CASSETTE_FIELD = LEGACY_PAYLOAD_BYTES,
    PAYLOAD_BYTES = CASSETTE_FIELD + 12
};

const char SESSION_JOURNAL_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/session.bin";

static const StoreKind KIND = {
    .magic = {'D', 'S', 'S', 'J'}, .version = 2, .inline_payload = true, .oldest_version = 1};

typedef struct Reader {
    const SessionJournalConfig *config;
    const uint32_t *older_than;
    SessionState state;
} Reader;

static StoreSlot slot_of(const SessionJournalConfig *config, uint8_t bank)
{
    return (StoreSlot){
        .file = &config->file, .kind = &KIND, .offset = (uint32_t)bank * SESSION_RECORD_BYTES};
}

static bool config_valid(const SessionJournalConfig *config)
{
    return config->file.source.read != NULL && config->file.size >= SESSION_FILE_BYTES &&
           config->track_limit <= UINT16_MAX;
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

static bool cassette_valid(const SessionState *state)
{
    const CassetteState *tape = &state->cassette;
    if (!tape->active) {
        return tape->generation == 0 && tape->count == 0 && tape->split == 0 && tape->slot == 0 &&
               tape->side == 0 && !tape->flip;
    }
    return state->queue_kind == SESSION_QUEUE_STORED && tape->slot < 32 && tape->side < 2 &&
           tape->split > 0 && tape->split < tape->count && tape->split <= CASSETTE_SIDE_CAPACITY &&
           tape->count - tape->split <= CASSETTE_SIDE_CAPACITY &&
           cassette_contains(tape, state->resume.cursor.position) &&
           (!tape->flip ||
            state->resume.cursor.position + 1u == (tape->side == 0 ? tape->split : tape->count));
}

static bool decode_cassette(const uint8_t *bytes, CassetteState *tape)
{
    *tape = (CassetteState){.generation = store_get32(bytes),
                            .count = store_get16(bytes + 4),
                            .split = store_get16(bytes + 6),
                            .slot = bytes[8],
                            .side = bytes[9],
                            .active = (bytes[10] & 1) != 0,
                            .flip = (bytes[10] & 2) != 0};
    return bytes[10] <= 3 && bytes[11] == 0;
}

static void encode_cassette(const CassetteState *tape, uint8_t *bytes)
{
    store_put32(bytes, tape->generation);
    store_put16(bytes + 4, tape->count);
    store_put16(bytes + 6, tape->split);
    bytes[8] = tape->slot;
    bytes[9] = tape->side;
    bytes[10] = (uint8_t)((tape->active ? 1 : 0) | (tape->flip ? 2 : 0));
}

static bool decode_payload(const SessionJournalConfig *config, const uint8_t *bytes,
                           SessionState *state, uint16_t version)
{
    *state = (SessionState){.order_generation = store_get32(bytes + ORDER_FIELD),
                            .queue_kind = (SessionQueueKind)bytes[KIND_FIELD]};
    return resume_state_decode(bytes, RESUME_STATE_SIZE, &state->resume) &&
           state->resume.library_fingerprint == config->fingerprint &&
           up_next_decode(bytes + RESUME_STATE_SIZE, UP_NEXT_STATE_SIZE, config->fingerprint,
                          &state->up_next) &&
           ids_valid(config, state) && kind_valid(state) &&
           (version == 1 || decode_cassette(bytes + CASSETTE_FIELD, &state->cassette)) &&
           cassette_valid(state);
}

static bool wanted(const Reader *reader, const StoreHeader *header)
{
    bool old_enough =
        reader->older_than == NULL || store_newer(*reader->older_than, header->generation);
    return old_enough && header->binding == reader->config->fingerprint;
}

static StoreLoad check_record(void *context, uint8_t bank, const StoreHeader *header)
{
    Reader *reader = context;
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader again;
    StoreSlot slot = slot_of(reader->config, bank);
    if (!wanted(reader, header)) {
        return STORE_EMPTY;
    }
    StoreLoad load = store_load(&slot, block, &again);
    if (load != STORE_READY) {
        return load;
    }
    bool decoded =
        again.generation == header->generation &&
        again.bytes == (again.version == 1 ? LEGACY_PAYLOAD_BYTES : PAYLOAD_BYTES) &&
        decode_payload(reader->config, block + STORE_FIELD_BYTES, &reader->state, again.version);
    return decoded ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad load_newest(SessionJournal *journal, const uint32_t *older_than,
                             SessionState *state)
{
    Reader reader = {.config = &journal->config, .older_than = older_than};
    StorePair pair = {.file = &journal->config.file,
                      .kind = &KIND,
                      .bank_bytes = SESSION_RECORD_BYTES,
                      .check = check_record,
                      .context = &reader};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    journal->needs_reload |= store_blocking(load);
    if (load == STORE_READY) {
        journal->generation = choice.header.generation;
        journal->bank = choice.bank;
        journal->valid = true;
        *state = reader.state;
    }
    return load;
}

StoreLoad session_journal_open(SessionJournal *journal, SessionJournalConfig config,
                               SessionState *state)
{
    *journal = (SessionJournal){.config = config};
    if (!config_valid(&config)) {
        journal->needs_reload = true;
        return STORE_UNREADABLE;
    }
    return load_newest(journal, NULL, state);
}

StoreLoad session_journal_previous(SessionJournal *journal, SessionState *state)
{
    if (!journal->valid) {
        return STORE_EMPTY;
    }
    uint32_t current = journal->generation;
    StoreLoad load = load_newest(journal, &current, state);
    return load == STORE_READY || store_blocking(load) ? load : STORE_EMPTY;
}

static uint32_t next_generation(const SessionJournal *journal)
{
    return journal->valid ? journal->generation + 1 : 1;
}

static void make_record(const SessionJournal *journal, const SessionState *state, uint8_t *block)
{
    uint8_t *payload = block + STORE_FIELD_BYTES;
    ResumeState resume = state->resume;
    if (state->queue_kind == SESSION_QUEUE_EMPTY) {
        resume.plan.source.group = LIBRARY_ARTISTS;
    }
    resume_state_encode(&resume, payload);
    up_next_encode(&state->up_next, journal->config.fingerprint, payload + RESUME_STATE_SIZE);
    store_put32(payload + ORDER_FIELD, state->order_generation);
    payload[KIND_FIELD] = (uint8_t)state->queue_kind;
    encode_cassette(&state->cassette, payload + CASSETTE_FIELD);
    StoreHeader header = {.generation = next_generation(journal),
                          .bytes = PAYLOAD_BYTES,
                          .binding = journal->config.fingerprint};
    store_encode(&KIND, &header, block);
}

static bool saveable(const SessionJournal *journal, const SessionState *state)
{
    return !journal->needs_reload && config_valid(&journal->config) &&
           journal->config.file.write != NULL && ids_valid(&journal->config, state) &&
           kind_valid(state) && cassette_valid(state);
}

StoreResult session_journal_save(SessionJournal *journal, const SessionState *state)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    SessionState check;
    if (!saveable(journal, state)) {
        return STORE_FAILED;
    }
    make_record(journal, state, block);
    if (!decode_payload(&journal->config, block + STORE_FIELD_BYTES, &check, 2)) {
        return STORE_FAILED;
    }
    uint8_t bank = journal->valid ? (uint8_t)(1 - journal->bank) : 0;
    uint32_t generation = next_generation(journal);
    StoreSlot slot = slot_of(&journal->config, bank);
    StoreResult result = store_publish(&slot, block, journal->valid ? &journal->generation : NULL);
    journal->needs_reload = result == STORE_UNCERTAIN;
    if (result == STORE_DONE) {
        journal->generation = generation;
        journal->bank = bank;
        journal->valid = true;
    }
    return result;
}
