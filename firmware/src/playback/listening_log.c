#include "playback/listening_log.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    ROOT_NEXT = 0,
    ROOT_KEPT = 4,
    ROOT_LISTENED = 8,
    ROOT_PLAYS = 16,
    ROOT_SKIPS = 20,
    ROOT_OUTPUTS = 24,
    ROOT_FORMATS = ROOT_OUTPUTS + 8 * LISTENING_OUTPUT_COUNT,
    ROOT_BOOTS = ROOT_FORMATS + 8 * LISTENING_FORMAT_COUNT,
    ROOT_CHARGES = ROOT_BOOTS + 4,
    ROOT_FULL = ROOT_CHARGES + 4,
    ROOT_BYTES = ROOT_FULL + 4
};

enum {
    RECORD_SEQUENCE = 0,
    RECORD_TRACK = 4,
    RECORD_ALBUM = 8,
    RECORD_ARTIST = 12,
    RECORD_LENGTH = 16,
    RECORD_LISTENED = 20,
    RECORD_START = 24,
    RECORD_END = 28,
    RECORD_UPTIME = 32,
    RECORD_CLOCK = 36,
    RECORD_BOOT = 40,
    RECORD_INDEX = 44,
    RECORD_ENDED = 46,
    RECORD_OUTPUT = 47,
    RECORD_FORMAT = 48,
    RECORD_CHECK = 60
};

enum {
    HIRES_RATE = 48000,
    HIRES_BITS = 16,
    PERMILLE = 1000
};

_Static_assert((LISTENING_LOG_CAPACITY & (LISTENING_LOG_CAPACITY - 1)) == 0,
               "sequence numbers stay in step with slots when they wrap");
_Static_assert(LISTENING_LOG_CAPACITY <= 32768, "a position in the ring fits 16 bits");
_Static_assert(LISTENING_LOG_CAPACITY % LISTENING_SECTOR_RECORDS == 0, "the ring ends on a sector");
_Static_assert((int)ROOT_BYTES <= (int)STORE_INLINE_BYTES, "the counters fit their block");

const char LISTENING_LOG_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/listens.bin";

static const StoreKind KIND = {.magic = {'D', 'S', 'L', 'L'}, .version = 1, .inline_payload = true};

typedef struct Reader {
    const StoreFile *file;
    ListeningRoot root;
} Reader;

static void put64(uint8_t *bytes, uint64_t value)
{
    store_put32(bytes, (uint32_t)value);
    store_put32(bytes + 4, (uint32_t)(value >> 32));
}

static uint64_t get64(const uint8_t *bytes)
{
    return (uint64_t)store_get32(bytes) | (uint64_t)store_get32(bytes + 4) << 32;
}

static void encode_root(const ListeningRoot *root, uint8_t *bytes)
{
    const ListeningCounters *counters = &root->counters;
    store_put32(bytes + ROOT_NEXT, root->next);
    store_put32(bytes + ROOT_KEPT, root->kept);
    put64(bytes + ROOT_LISTENED, counters->totals.listened_ms);
    store_put32(bytes + ROOT_PLAYS, counters->totals.plays);
    store_put32(bytes + ROOT_SKIPS, counters->totals.skips);
    for (int index = 0; index < LISTENING_OUTPUT_COUNT; index++) {
        put64(bytes + ROOT_OUTPUTS + 8 * index, counters->output_ms[index]);
    }
    for (int index = 0; index < LISTENING_FORMAT_COUNT; index++) {
        put64(bytes + ROOT_FORMATS + 8 * index, counters->format_ms[index]);
    }
    store_put32(bytes + ROOT_BOOTS, counters->boots);
    store_put32(bytes + ROOT_CHARGES, counters->charge_sessions);
    store_put32(bytes + ROOT_FULL, counters->full_charges);
}

static void decode_times(const uint8_t *bytes, ListeningCounters *counters)
{
    for (int index = 0; index < LISTENING_OUTPUT_COUNT; index++) {
        counters->output_ms[index] = get64(bytes + ROOT_OUTPUTS + 8 * index);
    }
    for (int index = 0; index < LISTENING_FORMAT_COUNT; index++) {
        counters->format_ms[index] = get64(bytes + ROOT_FORMATS + 8 * index);
    }
}

static bool decode_root(const uint8_t *bytes, ListeningRoot *root)
{
    ListeningCounters *counters = &root->counters;
    uint32_t kept = store_get32(bytes + ROOT_KEPT);
    root->next = store_get32(bytes + ROOT_NEXT);
    root->kept = (uint16_t)kept;
    counters->totals.listened_ms = get64(bytes + ROOT_LISTENED);
    counters->totals.plays = store_get32(bytes + ROOT_PLAYS);
    counters->totals.skips = store_get32(bytes + ROOT_SKIPS);
    decode_times(bytes, counters);
    counters->boots = store_get32(bytes + ROOT_BOOTS);
    counters->charge_sessions = store_get32(bytes + ROOT_CHARGES);
    counters->full_charges = store_get32(bytes + ROOT_FULL);
    return kept <= LISTENING_LOG_CAPACITY;
}

static void encode_record(const ListeningRecord *record, uint8_t *bytes)
{
    const ListeningPlay *play = &record->play;
    for (int index = 0; index < LISTENING_RECORD_BYTES; index++) {
        bytes[index] = 0;
    }
    store_put32(bytes + RECORD_SEQUENCE, record->sequence);
    store_put32(bytes + RECORD_TRACK, play->track.key);
    store_put32(bytes + RECORD_ALBUM, play->track.album);
    store_put32(bytes + RECORD_ARTIST, play->track.artist);
    store_put32(bytes + RECORD_LENGTH, play->length_ms);
    store_put32(bytes + RECORD_LISTENED, play->listened_ms);
    store_put32(bytes + RECORD_START, play->start_ms);
    store_put32(bytes + RECORD_END, play->end_ms);
    store_put32(bytes + RECORD_UPTIME, play->uptime_s);
    store_put32(bytes + RECORD_CLOCK, play->clock_s);
    store_put32(bytes + RECORD_BOOT, record->boot);
    store_put16(bytes + RECORD_INDEX, play->track.index);
    bytes[RECORD_ENDED] = (uint8_t)play->end;
    bytes[RECORD_OUTPUT] = (uint8_t)play->output;
    bytes[RECORD_FORMAT] = (uint8_t)play->format;
    store_put32(bytes + RECORD_CHECK, hash_update(HASH_START, bytes, RECORD_CHECK));
}

static bool kinds_valid(uint8_t end, uint8_t output, uint8_t format)
{
    return end < LISTENING_END_COUNT && output < LISTENING_OUTPUT_COUNT &&
           format < LISTENING_FORMAT_COUNT;
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
    play->clock_s = store_get32(bytes + RECORD_CLOCK);
    play->end = (ListeningEnd)bytes[RECORD_ENDED];
    play->output = (ListeningOutput)bytes[RECORD_OUTPUT];
    play->format = (ListeningFormat)bytes[RECORD_FORMAT];
}

static bool decode_record(const uint8_t *bytes, ListeningRecord *record)
{
    if (store_get32(bytes + RECORD_CHECK) != hash_update(HASH_START, bytes, RECORD_CHECK) ||
        !kinds_valid(bytes[RECORD_ENDED], bytes[RECORD_OUTPUT], bytes[RECORD_FORMAT])) {
        return false;
    }
    record->sequence = store_get32(bytes + RECORD_SEQUENCE);
    record->boot = store_get32(bytes + RECORD_BOOT);
    decode_play(bytes, &record->play);
    return true;
}

static uint32_t record_offset(uint32_t sequence)
{
    return LISTENING_RING_OFFSET + sequence % LISTENING_LOG_CAPACITY * LISTENING_RECORD_BYTES;
}

static StoreSlot slot_of(const StoreFile *file, uint8_t bank)
{
    return (StoreSlot){.file = file, .kind = &KIND, .offset = (uint32_t)bank * STORE_BLOCK_BYTES};
}

static bool file_valid(const StoreFile *file)
{
    return file->source.read != NULL && file->size >= LISTENING_LOG_FILE_BYTES;
}

static StoreLoad check_root(void *context, uint8_t bank, const StoreHeader *header)
{
    Reader *reader = context;
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader again;
    StoreSlot slot = slot_of(reader->file, bank);
    StoreLoad load = store_load(&slot, block, &again);
    if (load != STORE_READY) {
        return load;
    }
    bool decoded = again.generation == header->generation && again.bytes == ROOT_BYTES &&
                   decode_root(block + STORE_FIELD_BYTES, &reader->root);
    return decoded ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad load_root(ListeningLog *log)
{
    Reader reader = {.file = &log->file};
    StorePair pair = {.file = &log->file,
                      .kind = &KIND,
                      .bank_bytes = STORE_BLOCK_BYTES,
                      .check = check_root,
                      .context = &reader};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    if (load == STORE_READY) {
        log->root = reader.root;
        log->generation = choice.header.generation;
        log->bank = choice.bank;
        log->valid = true;
    }
    return load;
}

StoreLoad listening_log_open(ListeningLog *log, ListeningLogConfig config)
{
    *log = (ListeningLog){.file = config.file, .root = {.next = 1}, .enabled = config.enabled};
    StoreLoad load = file_valid(&log->file) ? load_root(log) : STORE_UNREADABLE;
    log->needs_reload = store_blocking(load);
    return load;
}

void listening_log_enable(ListeningLog *log, bool enabled)
{
    log->enabled = enabled;
}

static bool writable(const ListeningLog *log)
{
    return !log->needs_reload && file_valid(&log->file) && log->file.write != NULL;
}

static StoreResult publish(ListeningLog *log, const ListeningRoot *root)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    uint8_t bank = log->valid ? (uint8_t)(1 - log->bank) : 0;
    StoreHeader header = {.generation = log->valid ? log->generation + 1 : 1, .bytes = ROOT_BYTES};
    StoreSlot slot = slot_of(&log->file, bank);
    encode_root(root, block + STORE_FIELD_BYTES);
    store_encode(&KIND, &header, block);
    StoreResult result = store_publish(&slot, block, log->valid ? &log->generation : NULL);
    log->needs_reload = result == STORE_UNCERTAIN;
    if (result == STORE_DONE) {
        log->root = *root;
        log->generation = header.generation;
        log->bank = bank;
        log->valid = true;
    }
    return result;
}

static bool write_record(const ListeningLog *log, const ListeningRecord *record)
{
    uint8_t wanted[LISTENING_RECORD_BYTES];
    uint8_t found[LISTENING_RECORD_BYTES];
    uint32_t offset = record_offset(record->sequence);
    encode_record(record, wanted);
    (void)store_file_write(&log->file, offset, wanted, sizeof wanted);
    if (!store_file_read(&log->file, offset, found, sizeof found)) {
        return false;
    }
    for (int index = 0; index < LISTENING_RECORD_BYTES; index++) {
        if (wanted[index] != found[index]) {
            return false;
        }
    }
    return true;
}

void listening_totals_add(ListeningTotals *totals, const ListeningPlay *play)
{
    totals->listened_ms += play->listened_ms;
    totals->plays++;
    totals->skips += play->end == LISTENING_SKIPPED ? 1 : 0;
}

static void count_play(ListeningRoot *root, const ListeningPlay *play)
{
    root->next++;
    root->kept = (uint16_t)(root->kept < LISTENING_LOG_CAPACITY ? root->kept + 1 : root->kept);
    listening_totals_add(&root->counters.totals, play);
    root->counters.output_ms[play->output] += play->listened_ms;
    root->counters.format_ms[play->format] += play->listened_ms;
}

StoreResult listening_log_append(ListeningLog *log, const ListeningPlay *play)
{
    if (!log->enabled) {
        return STORE_IDLE;
    }
    if (!writable(log) ||
        !kinds_valid((uint8_t)play->end, (uint8_t)play->output, (uint8_t)play->format)) {
        return STORE_FAILED;
    }
    ListeningRoot root = log->root;
    ListeningRecord record = {.play = *play, .sequence = root.next, .boot = root.counters.boots};
    if (!write_record(log, &record)) {
        return STORE_FAILED;
    }
    count_play(&root, play);
    StoreResult result = publish(log, &root);
    if (result == STORE_DONE) {
        listening_totals_add(&log->session, play);
    }
    return result;
}

StoreResult listening_log_note(ListeningLog *log, ListeningEvent event)
{
    if (!log->enabled) {
        return STORE_IDLE;
    }
    if (!writable(log)) {
        return STORE_FAILED;
    }
    ListeningRoot root = log->root;
    switch (event) {
    case LISTENING_BOOT:
        root.counters.boots++;
        break;
    case LISTENING_CHARGE_STARTED:
        root.counters.charge_sessions++;
        break;
    case LISTENING_CHARGE_FULL:
        root.counters.full_charges++;
        break;
    }
    return publish(log, &root);
}

ListeningFormat listening_format_class(bool lossless, uint32_t sample_rate, uint8_t bits)
{
    if (!lossless) {
        return LISTENING_FORMAT_LOSSY;
    }
    return sample_rate > HIRES_RATE || bits > HIRES_BITS ? LISTENING_FORMAT_HIRES
                                                         : LISTENING_FORMAT_LOSSLESS;
}

bool listening_play_listened(const ListeningPlay *play)
{
    if (play->length_ms == 0) {
        return play->end == LISTENING_FINISHED;
    }
    return play->listened_ms >= play->length_ms - play->length_ms / 2;
}

uint16_t listening_skip_permille(const ListeningTotals *totals)
{
    uint32_t plays = totals->plays;
    uint32_t skips = totals->skips;
    while (plays > UINT32_MAX / PERMILLE) {
        plays >>= 1;
        skips >>= 1;
    }
    return plays == 0 ? 0 : (uint16_t)(skips * PERMILLE / plays);
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
    if (count == 0 || !store_file_read(cursor->file, record_offset(sequence), block,
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
           store_file_read(cursor->file, record_offset(sequence), bytes, sizeof bytes) &&
           decode_record(bytes, record) && record->sequence == sequence;
}
