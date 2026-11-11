#include "playback/moments.h"

#include <stddef.h>

static const StoreKind KIND = {.magic = {'D', 'S', 'M', 'O'}, .version = 1};

enum {
    POSITION_FIELD = MIX_RECORD_MAX,
    LABEL_FIELD = POSITION_FIELD + 4
};

static StoreSlot slot_of(const Moments *moments, uint8_t bank)
{
    return (StoreSlot){&moments->file, &KIND, (uint32_t)bank * MOMENTS_BANK_BYTES};
}

static uint32_t entry_offset(uint8_t bank, uint16_t index)
{
    return (uint32_t)bank * MOMENTS_BANK_BYTES + STORE_BLOCK_BYTES +
           (uint32_t)index * MOMENTS_ENTRY_BYTES;
}

static bool label_valid(const char *label)
{
    for (int index = 0; index < MOMENT_LABEL_BYTES; index++) {
        uint8_t value = (uint8_t)label[index];
        if (value == 0) {
            return index > 0;
        }
        if (value < 32 || value == 127) {
            return false;
        }
    }
    return false;
}

static bool valid(const Moment *moment)
{
    const MixIdentity *identity = &moment->identity;
    return mix_identity_size(identity) != 0 && label_valid(moment->label) &&
           (identity->end_ms == 0 || moment->position_ms <= identity->end_ms - identity->start_ms);
}

static void encode(const Moment *moment, uint8_t *bytes)
{
    for (int index = 0; index < MOMENTS_ENTRY_BYTES; index++) {
        bytes[index] = 0;
    }
    (void)mix_identity_encode(&moment->identity, bytes);
    store_put32(bytes + POSITION_FIELD, moment->position_ms);
    for (int index = 0; index < MOMENT_LABEL_BYTES && moment->label[index] != 0; index++) {
        bytes[LABEL_FIELD + index] = (uint8_t)moment->label[index];
    }
}

static bool decode(const uint8_t *bytes, Moment *moment)
{
    *moment = (Moment){.position_ms = store_get32(bytes + POSITION_FIELD)};
    uint16_t used = mix_identity_decode(bytes, MIX_RECORD_MAX, &moment->identity);
    if (used == 0) {
        return false;
    }
    for (uint16_t index = used; index < MIX_RECORD_MAX; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    for (int index = 0; index < MOMENT_LABEL_BYTES; index++) {
        moment->label[index] = (char)bytes[LABEL_FIELD + index];
    }
    return valid(moment);
}

static StoreLoad read_entry(const Moments *moments, uint8_t bank, uint16_t index, Moment *moment)
{
    uint8_t bytes[MOMENTS_ENTRY_BYTES];
    if (!store_file_read(&moments->file, entry_offset(bank, index), bytes, sizeof bytes)) {
        return STORE_UNREADABLE;
    }
    return decode(bytes, moment) ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad check_bank(void *context, uint8_t bank, const StoreHeader *header)
{
    const Moments *moments = context;
    if (header->count > MOMENTS_CAPACITY ||
        header->bytes != (uint32_t)header->count * MOMENTS_ENTRY_BYTES || header->binding != 0) {
        return STORE_CORRUPT;
    }
    StoreSlot slot = slot_of(moments, bank);
    StoreScan scan = store_scan_begin();
    uint8_t block[STORE_BLOCK_BYTES];
    while (scan.position < header->bytes) {
        if (!store_scan_next(&slot, &scan, header->bytes, block)) {
            return STORE_UNREADABLE;
        }
    }
    if (scan.hash != header->hash) {
        return STORE_CORRUPT;
    }
    for (uint16_t index = 0; index < header->count; index++) {
        Moment moment;
        StoreLoad load = read_entry(moments, bank, index, &moment);
        if (load != STORE_READY) {
            return load;
        }
    }
    return STORE_READY;
}

StoreLoad moments_open(Moments *moments, StoreFile file)
{
    *moments = (Moments){.file = file};
    StorePair pair = {&moments->file, &KIND, MOMENTS_BANK_BYTES, check_bank, moments};
    StoreChoice choice;
    StoreLoad load =
        file.size < MOMENTS_FILE_BYTES ? STORE_UNREADABLE : store_pair_open(&pair, &choice);
    moments->needs_reload = load != STORE_READY && load != STORE_EMPTY;
    if (load == STORE_READY) {
        moments->valid = true;
        moments->generation = choice.header.generation;
        moments->bank = choice.bank;
        moments->count = choice.header.count;
    }
    return load;
}

bool moments_read(const Moments *moments, uint16_t index, Moment *moment)
{
    return moments->valid && !moments->needs_reload && index < moments->count &&
           read_entry(moments, moments->bank, index, moment) == STORE_READY;
}

static bool editable(Moments *moments, uint32_t revision)
{
    if (moments->busy || moments->needs_reload || moments->generation != revision ||
        moments->file.write == NULL) {
        return false;
    }
    Moments current;
    StoreLoad load = moments_open(&current, moments->file);
    bool unchanged = (load == STORE_READY || load == STORE_EMPTY) &&
                     current.generation == moments->generation && current.valid == moments->valid;
    moments->needs_reload = !unchanged;
    return unchanged;
}

static void begin(MomentsJob *job, Moments *moments, uint16_t count)
{
    job->moments = moments;
    job->bank = moments->valid ? (uint8_t)(1 - moments->bank) : 0;
    job->write.header = (StoreHeader){.generation = moments->valid ? moments->generation + 1 : 1,
                                      .bytes = (uint32_t)count * MOMENTS_ENTRY_BYTES,
                                      .count = count};
    store_write_begin(&job->write, moments->valid ? &moments->generation : NULL);
    moments->busy = true;
}

bool moments_add(MomentsJob *job, Moments *moments, uint32_t revision, const Moment *moment)
{
    if (moments->count >= MOMENTS_CAPACITY || !valid(moment) || !editable(moments, revision)) {
        return false;
    }
    *job = (MomentsJob){.moment = *moment};
    begin(job, moments, (uint16_t)(moments->count + 1));
    return true;
}

bool moments_delete(MomentsJob *job, Moments *moments, uint32_t revision, uint16_t index)
{
    if (index >= moments->count || !editable(moments, revision)) {
        return false;
    }
    *job = (MomentsJob){.index = index, .deleting = true};
    begin(job, moments, (uint16_t)(moments->count - 1));
    return true;
}

static bool fill(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    const MomentsJob *job = context;
    uint8_t encoded[MOMENTS_ENTRY_BYTES];
    if (!job->deleting) {
        encode(&job->moment, encoded);
    }
    for (uint32_t done = 0; done < count;) {
        uint32_t at = position + done;
        uint16_t index = (uint16_t)(at / MOMENTS_ENTRY_BYTES);
        uint32_t within = at % MOMENTS_ENTRY_BYTES;
        uint32_t amount = MOMENTS_ENTRY_BYTES - within;
        amount = amount < count - done ? amount : count - done;
        if (!job->deleting && index == 0) {
            for (uint32_t byte = 0; byte < amount; byte++) {
                block->bytes[done + byte] = encoded[within + byte];
            }
        } else {
            uint16_t source =
                job->deleting ? (uint16_t)(index + (index >= job->index)) : (uint16_t)(index - 1);
            if (!store_file_read(&job->moments->file,
                                 entry_offset(job->moments->bank, source) + within,
                                 block->bytes + done, amount)) {
                return false;
            }
        }
        done += amount;
    }
    return true;
}

StoreResult moments_step(MomentsJob *job)
{
    if (job->write.result != STORE_WORKING) {
        return job->write.result;
    }
    Moments *moments = job->moments;
    StoreSlot slot = slot_of(moments, job->bank);
    StoreResult result = store_write_step(&job->write, &slot, (StoreFill){fill, job});
    if (result != STORE_WORKING) {
        moments->busy = false;
        moments->needs_reload |= result == STORE_UNCERTAIN;
        if (result == STORE_DONE) {
            moments->valid = true;
            moments->bank = job->bank;
            moments->generation = job->write.header.generation;
            moments->count = job->write.header.count;
        }
    }
    return result;
}

void moments_abort(MomentsJob *job)
{
    if (job->write.result == STORE_WORKING) {
        store_write_abort(&job->write);
        job->moments->busy = false;
    }
}

void moment_resolve_begin(MomentResolve *job, const Library *library, const MixIdentity *identity)
{
    *job = (MomentResolve){.library = library, .identity = identity, .result = STORE_WORKING};
}

StoreResult moment_resolve_step(MomentResolve *job)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    uint32_t count = library_count(job->library, LIBRARY_TRACKS);
    for (int work = 0; work < 32 && job->index < count; work++, job->index++) {
        MixIdentity identity;
        StoreLoad load = mix_identity_read(job->library, (uint16_t)job->index, &identity);
        if (load == STORE_UNREADABLE) {
            job->result = STORE_FAILED;
            return job->result;
        }
        if (load == STORE_READY && mix_identity_equal(&identity, job->identity)) {
            job->track = (uint16_t)job->index;
            if (++job->matches == 2) {
                job->result = STORE_FAILED;
                return job->result;
            }
        }
    }
    if (job->index == count) {
        job->result = job->matches == 1 ? STORE_DONE : STORE_FAILED;
    }
    return job->result;
}
