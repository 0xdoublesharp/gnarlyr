#include "playback/listening_log.h"

#include <stddef.h>

#include "core/hash.h"

#include "listening_record.h"
#include "listening_root.h"

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

static void put64(uint8_t *bytes, uint64_t value)
{
    store_put32(bytes, (uint32_t)value);
    store_put32(bytes + 4, (uint32_t)(value >> 32));
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
    if (play->clock_s != 0) {
        bytes[RECORD_CLOCK_FLAGS] = CLOCK_UTC;
        store_put16(bytes + RECORD_OFFSET, (uint16_t)play->offset_minutes);
    }
    store_put32(bytes + RECORD_CHECK, hash_update(HASH_START, bytes, RECORD_CHECK));
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
    uint32_t offset = listening_record_offset(record->sequence);
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
        !listening_kinds_valid((uint8_t)play->end, (uint8_t)play->output, (uint8_t)play->format)) {
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
