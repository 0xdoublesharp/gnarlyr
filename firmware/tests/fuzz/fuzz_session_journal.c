#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/hash.h"
#include "fuzz_source.h"
#include "playback/session_journal.h"

enum {
    JOURNAL_PAYLOAD_BYTES = RESUME_STATE_SIZE + UP_NEXT_STATE_SIZE + 8,
    CHECKSUM_BYTES = 4
};

static const StoreKind JOURNAL_KIND = {
    .magic = {'D', 'S', 'S', 'J'}, .version = 1, .inline_payload = true};

typedef struct FuzzInput {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
} FuzzInput;

typedef struct FuzzDisk {
    uint8_t bytes[SESSION_FILE_BYTES];
    uint32_t write_limit;
    bool write_result;
    bool refuse_reads;
    bool refuse_after_write;
} FuzzDisk;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t next_value(FuzzInput *input)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < 4 && input->offset < input->size; index++) {
        value |= (uint32_t)input->bytes[input->offset++] << (8u * index);
    }
    return value;
}

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    FuzzDisk *disk = context;
    if (disk->refuse_reads || offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        return false;
    }
    memcpy(bytes, disk->bytes + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    FuzzDisk *disk = context;
    if (offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        return false;
    }
    uint32_t amount = length < disk->write_limit ? length : disk->write_limit;
    memcpy(disk->bytes + offset, bytes, amount);
    disk->refuse_reads = disk->refuse_after_write;
    return disk->write_result;
}

static SessionJournalConfig configuration(FuzzDisk *disk, uint32_t fingerprint,
                                          uint32_t track_limit)
{
    return (SessionJournalConfig){
        .file = {.source = {read_disk, disk}, .write = write_disk, .size = sizeof disk->bytes},
        .fingerprint = fingerprint,
        .track_limit = track_limit};
}

static SessionState make_state(FuzzInput *input, uint32_t fingerprint, uint32_t track_limit)
{
    SessionState state = {
        .resume = {.library_fingerprint = fingerprint,
                   .plan = {.source = {.kind = BROWSE_ALBUM_TRACKS,
                                       .group = LIBRARY_ARTISTS,
                                       .subject = (uint16_t)next_value(input),
                                       .artist = LIBRARY_NONE},
                            .anchor_track = (uint16_t)(next_value(input) % track_limit),
                            .shuffle = (ShuffleMode)(next_value(input) % SHUFFLE_MODE_COUNT),
                            .seed = next_value(input)},
                   .repeat = (RepeatMode)(next_value(input) % REPEAT_MODE_COUNT),
                   .volume = (uint8_t)(next_value(input) % 51u),
                   .track = (uint16_t)(next_value(input) % track_limit),
                   .position_ms = next_value(input)},
        .queue_kind = (SessionQueueKind)(next_value(input) % 3u)};
    if (state.queue_kind == SESSION_QUEUE_STORED) {
        state.order_generation = next_value(input);
    } else if (state.queue_kind == SESSION_QUEUE_EMPTY) {
        state.resume.from_up_next = true;
    } else {
        uint32_t selected = next_value(input) % 5u;
        uint32_t counts[] = {0, 1, 4, UP_NEXT_CAPACITY - 1, UP_NEXT_CAPACITY};
        uint32_t count = counts[selected];
        for (uint32_t index = 0; index < count; index++) {
            fuzz_require(up_next_add(&state.up_next, (uint16_t)(next_value(input) % track_limit)));
        }
    }
    return state;
}

static bool same_state(const SessionState *first, const SessionState *second)
{
    uint8_t first_resume[RESUME_STATE_SIZE];
    uint8_t second_resume[RESUME_STATE_SIZE];
    uint8_t first_next[UP_NEXT_STATE_SIZE];
    uint8_t second_next[UP_NEXT_STATE_SIZE];
    resume_state_encode(&first->resume, first_resume);
    resume_state_encode(&second->resume, second_resume);
    up_next_encode(&first->up_next, first->resume.library_fingerprint, first_next);
    up_next_encode(&second->up_next, second->resume.library_fingerprint, second_next);
    return memcmp(first_resume, second_resume, sizeof first_resume) == 0 &&
           memcmp(first_next, second_next, sizeof first_next) == 0 &&
           first->order_generation == second->order_generation &&
           first->queue_kind == second->queue_kind;
}

static void check_state(const SessionState *state, uint32_t track_limit)
{
    fuzz_require(state->resume.track < track_limit);
    fuzz_require(state->up_next.count <= UP_NEXT_CAPACITY);
    for (uint16_t index = 0; index < state->up_next.count; index++) {
        fuzz_require(state->up_next.tracks[index] < track_limit);
    }
    if (state->queue_kind == SESSION_QUEUE_GENERATED) {
        fuzz_require(state->order_generation == 0);
    } else if (state->queue_kind == SESSION_QUEUE_STORED) {
        fuzz_require(!state->resume.from_up_next && state->resume.cursor.album_track == 0 &&
                     state->up_next.count == 0);
    } else {
        fuzz_require(state->queue_kind == SESSION_QUEUE_EMPTY);
        fuzz_require(state->resume.from_up_next && state->order_generation == 0);
    }
}

static void check_raw(const uint8_t *data, size_t size)
{
    FuzzDisk disk = {.write_limit = SESSION_RECORD_BYTES, .write_result = true};
    uint32_t length = size < sizeof disk.bytes ? (uint32_t)size : (uint32_t)sizeof disk.bytes;
    memcpy(disk.bytes, data, length);
    SessionJournal journal;
    SessionState state;
    StoreLoad load = session_journal_open(&journal, configuration(&disk, 1, 32), &state);
    if (load == STORE_READY) {
        check_state(&state, 32);
        if (session_journal_previous(&journal, &state) == STORE_READY) {
            check_state(&state, 32);
        }
    }
}

static void reseal_nested(uint8_t *payload, uint32_t selected)
{
    if ((selected & 1u) != 0) {
        store_put32(payload + RESUME_STATE_SIZE - CHECKSUM_BYTES,
                    hash_update(HASH_START, payload, RESUME_STATE_SIZE - CHECKSUM_BYTES));
    }
    if ((selected & 2u) != 0) {
        uint8_t *up_next = payload + RESUME_STATE_SIZE;
        store_put32(up_next + UP_NEXT_STATE_SIZE - CHECKSUM_BYTES,
                    hash_update(HASH_START, up_next, UP_NEXT_STATE_SIZE - CHECKSUM_BYTES));
    }
}

static void check_mutated(FuzzDisk disk, SessionJournalConfig config, FuzzInput *input)
{
    StoreHeader header;
    fuzz_require(store_decode(&JOURNAL_KIND, disk.bytes, &header) == STORE_READY);
    uint8_t *payload = disk.bytes + STORE_FIELD_BYTES;
    uint32_t mutations = next_value(input) % 9u;
    for (uint32_t index = 0; index < mutations; index++) {
        uint32_t offset = next_value(input) % JOURNAL_PAYLOAD_BYTES;
        payload[offset] ^= (uint8_t)next_value(input);
    }
    reseal_nested(payload, next_value(input));
    store_encode(&JOURNAL_KIND, &header, disk.bytes);
    config.file.source.context = &disk;
    SessionJournal journal;
    SessionState restored;
    StoreLoad load = session_journal_open(&journal, config, &restored);
    fuzz_require(load == STORE_READY || load == STORE_CORRUPT);
    if (load == STORE_READY) {
        check_state(&restored, config.track_limit);
    }
}

static void check_fault(FuzzDisk *disk, SessionJournalConfig config, SessionState first,
                        SessionState second, FuzzInput *input)
{
    SessionJournal journal;
    SessionState restored;
    fuzz_require(session_journal_open(&journal, config, &restored) == STORE_READY);
    disk->write_limit = next_value(input) % (SESSION_RECORD_BYTES + 1u);
    disk->write_result = (next_value(input) & 1u) != 0;
    disk->refuse_after_write = (next_value(input) & 1u) != 0;
    StoreResult result = session_journal_save(&journal, &second);
    if (result == STORE_UNCERTAIN) {
        uint8_t uncertain[SESSION_FILE_BYTES];
        memcpy(uncertain, disk->bytes, sizeof uncertain);
        fuzz_require(journal.needs_reload);
        fuzz_require(session_journal_save(&journal, &second) == STORE_FAILED);
        fuzz_require(memcmp(uncertain, disk->bytes, sizeof uncertain) == 0);
    }
    disk->refuse_reads = false;
    disk->refuse_after_write = false;
    fuzz_require(session_journal_open(&journal, config, &restored) == STORE_READY);
    check_state(&restored, config.track_limit);
    if (result == STORE_DONE) {
        fuzz_require(same_state(&restored, &second));
    } else if (result == STORE_FAILED) {
        fuzz_require(same_state(&restored, &first));
    } else {
        fuzz_require(result == STORE_UNCERTAIN);
        fuzz_require(same_state(&restored, &first) || same_state(&restored, &second));
    }
}

static void check_round_trip(FuzzInput *input)
{
    FuzzDisk disk = {.write_limit = SESSION_RECORD_BYTES, .write_result = true};
    uint32_t fingerprint = next_value(input);
    uint32_t track_limit = 1u + next_value(input) % 32u;
    SessionJournalConfig config = configuration(&disk, fingerprint, track_limit);
    SessionState first = make_state(input, fingerprint, track_limit);
    SessionState second = make_state(input, fingerprint, track_limit);
    SessionJournal journal;
    SessionState restored;
    fuzz_require(session_journal_open(&journal, config, &restored) == STORE_EMPTY);
    fuzz_require(session_journal_save(&journal, &first) == STORE_DONE);
    fuzz_require(session_journal_open(&journal, config, &restored) == STORE_READY);
    fuzz_require(same_state(&restored, &first));
    check_mutated(disk, config, input);
    uint8_t first_disk[SESSION_FILE_BYTES];
    memcpy(first_disk, disk.bytes, sizeof first_disk);
    fuzz_require(session_journal_save(&journal, &second) == STORE_DONE);
    fuzz_require(session_journal_previous(&journal, &restored) == STORE_READY);
    fuzz_require(same_state(&restored, &first));
    memcpy(disk.bytes, first_disk, sizeof first_disk);
    check_fault(&disk, config, first, second, input);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    check_raw(data, size);
    FuzzInput input = {.bytes = data, .size = size};
    check_round_trip(&input);
    return 0;
}
