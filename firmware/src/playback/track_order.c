#include "playback/track_order.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    MAGIC = 0x524F5445,
    VERSION = 1,
    HEADER_HASH = 24,
    HEADER_FIELDS = 28
};

enum {
    STAGE_INVALIDATE,
    STAGE_WRITE,
    STAGE_VERIFY,
    STAGE_COMMIT
};

typedef enum BankState {
    BANK_INVALID,
    BANK_READY,
    BANK_UNREADABLE,
    BANK_UNSUPPORTED
} BankState;

typedef struct BankInfo {
    BankState state;
    uint32_t generation;
    uint32_t hash;
    uint16_t count;
} BankInfo;

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

static uint32_t data_offset(uint8_t bank, uint32_t position)
{
    return (uint32_t)bank * TRACK_ORDER_BANK_BYTES + TRACK_ORDER_HEADER_BYTES + 2 * position;
}

static uint16_t block_tracks(uint32_t remaining)
{
    return (uint16_t)(remaining > TRACK_ORDER_BLOCK_TRACKS ? TRACK_ORDER_BLOCK_TRACKS : remaining);
}

static bool config_valid(const TrackOrderConfig *config)
{
    return config->source.read != NULL && config->size >= TRACK_ORDER_FILE_BYTES &&
           config->track_limit <= TRACK_ORDER_CAPACITY;
}

static bool header_prefix_valid(const uint8_t *header)
{
    return get32(header, 0) == MAGIC &&
           get32(header, HEADER_HASH) == hash_update(HASH_START, header, HEADER_HASH);
}

static bool header_valid(const TrackOrderConfig *config, const uint8_t *header)
{
    return header_prefix_valid(header) && get16(header, 4) == VERSION &&
           get32(header, 8) == config->fingerprint &&
           get32(header, 16) == (uint32_t)get16(header, 6) * 2;
}

static bool ids_valid(const TrackOrderConfig *config, const uint8_t *bytes, uint16_t count)
{
    for (uint16_t index = 0; index < count; index++) {
        if (get16(bytes, 2u * index) >= config->track_limit) {
            return false;
        }
    }
    return true;
}

static BankState check_payload(const TrackOrderConfig *config, uint8_t bank, BankInfo info)
{
    uint8_t bytes[2 * TRACK_ORDER_BLOCK_TRACKS];
    uint32_t hash = HASH_START;
    for (uint32_t position = 0; position < info.count;) {
        uint16_t count = block_tracks(info.count - position);
        if (!config->source.read(config->source.context, data_offset(bank, position), bytes,
                                 2u * count)) {
            return BANK_UNREADABLE;
        }
        if (!ids_valid(config, bytes, count)) {
            return BANK_INVALID;
        }
        hash = hash_update(hash, bytes, 2u * count);
        position += count;
    }
    return hash == info.hash ? BANK_READY : BANK_INVALID;
}

static BankInfo read_bank(const TrackOrderConfig *config, uint8_t bank)
{
    uint8_t bytes[TRACK_ORDER_HEADER_BYTES];
    if (!config->source.read(config->source.context, (uint32_t)bank * TRACK_ORDER_BANK_BYTES, bytes,
                             sizeof bytes)) {
        return (BankInfo){.state = BANK_UNREADABLE};
    }
    if (header_prefix_valid(bytes) && get16(bytes, 4) != VERSION) {
        return (BankInfo){.state = BANK_UNSUPPORTED};
    }
    if (!header_valid(config, bytes)) {
        return (BankInfo){.state = BANK_INVALID};
    }
    BankInfo info = {
        .generation = get32(bytes, 12), .count = get16(bytes, 6), .hash = get32(bytes, 20)};
    info.state = check_payload(config, bank, info);
    return info;
}

static bool newer(uint32_t candidate, uint32_t previous)
{
    uint32_t distance = candidate - previous;
    return distance != 0 && distance < UINT32_C(0x80000000);
}

TrackOrderLoad track_order_open(TrackOrder *order, TrackOrderConfig config)
{
    *order = (TrackOrder){.config = config};
    if (!config_valid(&config)) {
        order->needs_reload = true;
        return TRACK_ORDER_UNREADABLE;
    }
    BankInfo banks[2] = {read_bank(&config, 0), read_bank(&config, 1)};
    if (banks[0].state == BANK_UNREADABLE || banks[1].state == BANK_UNREADABLE) {
        order->needs_reload = true;
        return TRACK_ORDER_UNREADABLE;
    }
    if (banks[0].state == BANK_UNSUPPORTED || banks[1].state == BANK_UNSUPPORTED) {
        order->needs_reload = true;
        return TRACK_ORDER_UNSUPPORTED;
    }
    if (banks[0].state != BANK_READY && banks[1].state != BANK_READY) {
        return TRACK_ORDER_EMPTY;
    }
    uint8_t bank = banks[1].state == BANK_READY && (banks[0].state != BANK_READY ||
                                                    newer(banks[1].generation, banks[0].generation))
                       ? 1
                       : 0;
    order->generation = banks[bank].generation;
    order->count = banks[bank].count;
    order->bank = bank;
    order->valid = true;
    return TRACK_ORDER_READY;
}

bool track_order_read(const TrackOrder *order, uint16_t first, uint16_t count, uint16_t *tracks)
{
    if (!order->valid || first > order->count || count > order->count - first ||
        (count > 0 && tracks == NULL)) {
        return false;
    }
    uint8_t bytes[2 * TRACK_ORDER_BLOCK_TRACKS];
    for (uint32_t position = 0; position < count;) {
        uint16_t block = block_tracks(count - position);
        const ByteSource *source = &order->config.source;
        if (!source->read(source->context, data_offset(order->bank, first + position), bytes,
                          2u * block) ||
            !ids_valid(&order->config, bytes, block)) {
            return false;
        }
        for (uint16_t index = 0; index < block; index++) {
            tracks[position + index] = get16(bytes, 2u * index);
        }
        position += block;
    }
    return true;
}

static bool source_read(const void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    return track_order_read(context, first, count, tracks);
}

TrackOrderSource track_order_source(const TrackOrder *order)
{
    return (TrackOrderSource){.read = source_read, .context = order, .count = order->count};
}

static bool edit_count(TrackOrderSource source, TrackOrderEdit edit, uint16_t *count)
{
    *count = source.count;
    switch (edit.operation) {
    case TRACK_ORDER_COPY:
        return source.count == 0 || source.read != NULL;
    case TRACK_ORDER_MOVE:
        return source.read != NULL && edit.from < source.count && edit.to < source.count;
    case TRACK_ORDER_REMOVE:
        *count = (uint16_t)(source.count - 1);
        return source.read != NULL && edit.from < source.count;
    case TRACK_ORDER_INSERT:
        *count = (uint16_t)(source.count + 1);
        return (source.count == 0 || source.read != NULL) && source.count < TRACK_ORDER_CAPACITY &&
               edit.from <= source.count;
    case TRACK_ORDER_CLEAR:
        *count = 0;
        return true;
    }
    return false;
}

bool track_order_job_begin(TrackOrderJob *job, TrackOrder *order, TrackOrderSource source,
                           TrackOrderEdit edit)
{
    uint16_t count = 0;
    if (order->busy || order->needs_reload || !config_valid(&order->config) ||
        order->config.write == NULL || !edit_count(source, edit, &count) ||
        (edit.operation == TRACK_ORDER_INSERT && edit.track >= order->config.track_limit)) {
        return false;
    }
    *job = (TrackOrderJob){.order = order,
                           .source = source,
                           .edit = edit,
                           .result = TRACK_ORDER_WORKING,
                           .generation = order->valid ? order->generation + 1 : 1,
                           .count = count,
                           .bank = order->valid ? (uint8_t)(1 - order->bank) : 0,
                           .hash = HASH_START,
                           .verify_hash = HASH_START,
                           .stage = STAGE_INVALIDATE};
    order->busy = true;
    return true;
}

static uint32_t source_index(const TrackOrderJob *job, uint32_t target)
{
    uint32_t from = job->edit.from;
    uint32_t to = job->edit.to;
    switch (job->edit.operation) {
    case TRACK_ORDER_INSERT:
        return target == from ? UINT32_MAX : target > from ? target - 1 : target;
    case TRACK_ORDER_REMOVE:
        return target >= from ? target + 1 : target;
    case TRACK_ORDER_MOVE:
        if (target == to) {
            return from;
        }
        if (from < to && target >= from && target < to) {
            return target + 1;
        }
        return from > to && target > to && target <= from ? target - 1 : target;
    case TRACK_ORDER_COPY:
    case TRACK_ORDER_CLEAR:
        return target;
    }
    return UINT32_MAX;
}

static uint16_t run_length(const TrackOrderJob *job, uint32_t first, uint16_t available)
{
    uint32_t source = source_index(job, first);
    uint16_t count = 1;
    while (count < available && source_index(job, first + count) == source + count) {
        count++;
    }
    return count;
}

static bool fill_block(const TrackOrderJob *job, uint16_t count, uint16_t *tracks)
{
    for (uint16_t position = 0; position < count;) {
        uint32_t source = source_index(job, job->position + position);
        if (source == UINT32_MAX) {
            tracks[position++] = job->edit.track;
            continue;
        }
        uint16_t run = run_length(job, job->position + position, (uint16_t)(count - position));
        if (!job->source.read(job->source.context, (uint16_t)source, run, tracks + position)) {
            return false;
        }
        position = (uint16_t)(position + run);
    }
    return true;
}

static TrackOrderResult finish_job(TrackOrderJob *job, TrackOrderResult result)
{
    job->result = result;
    job->order->busy = false;
    job->order->needs_reload |= result == TRACK_ORDER_UNCERTAIN;
    return result;
}

static TrackOrderResult invalidate(TrackOrderJob *job)
{
    uint8_t bytes[TRACK_ORDER_HEADER_BYTES] = {0};
    TrackOrderConfig *config = &job->order->config;
    if (!config->write(config->source.context, (uint32_t)job->bank * TRACK_ORDER_BANK_BYTES, bytes,
                       sizeof bytes)) {
        return finish_job(job, TRACK_ORDER_FAILED);
    }
    job->stage = STAGE_WRITE;
    return TRACK_ORDER_WORKING;
}

static TrackOrderResult write_block(TrackOrderJob *job)
{
    uint16_t tracks[TRACK_ORDER_BLOCK_TRACKS];
    uint16_t count = block_tracks(job->count - job->position);
    if (count == 0) {
        job->position = 0;
        job->stage = STAGE_VERIFY;
        return TRACK_ORDER_WORKING;
    }
    if (!fill_block(job, count, tracks)) {
        return finish_job(job, TRACK_ORDER_FAILED);
    }
    uint8_t *bytes = (uint8_t *)tracks;
    for (uint16_t index = 0; index < count; index++) {
        if (tracks[index] >= job->order->config.track_limit) {
            return finish_job(job, TRACK_ORDER_FAILED);
        }
        put16(bytes, 2u * index, tracks[index]);
    }
    TrackOrderConfig *config = &job->order->config;
    if (!config->write(config->source.context, data_offset(job->bank, job->position), bytes,
                       2u * count)) {
        return finish_job(job, TRACK_ORDER_FAILED);
    }
    job->hash = hash_update(job->hash, bytes, 2u * count);
    job->position += count;
    return TRACK_ORDER_WORKING;
}

static TrackOrderResult verify_block(TrackOrderJob *job)
{
    uint8_t bytes[2 * TRACK_ORDER_BLOCK_TRACKS];
    uint16_t count = block_tracks(job->count - job->position);
    if (count == 0) {
        if (job->verify_hash != job->hash) {
            return finish_job(job, TRACK_ORDER_FAILED);
        }
        job->stage = STAGE_COMMIT;
        return TRACK_ORDER_WORKING;
    }
    const ByteSource *source = &job->order->config.source;
    if (!source->read(source->context, data_offset(job->bank, job->position), bytes, 2u * count)) {
        return finish_job(job, TRACK_ORDER_FAILED);
    }
    job->verify_hash = hash_update(job->verify_hash, bytes, 2u * count);
    job->position += count;
    return TRACK_ORDER_WORKING;
}

static void make_header(const TrackOrderJob *job, uint8_t *bytes)
{
    for (int index = 0; index < TRACK_ORDER_HEADER_BYTES; index++) {
        bytes[index] = 0;
    }
    put32(bytes, 0, MAGIC);
    put16(bytes, 4, VERSION);
    put16(bytes, 6, job->count);
    put32(bytes, 8, job->order->config.fingerprint);
    put32(bytes, 12, job->generation);
    put32(bytes, 16, (uint32_t)job->count * 2);
    put32(bytes, 20, job->hash);
    put32(bytes, HEADER_HASH, hash_update(HASH_START, bytes, HEADER_HASH));
}

static bool same_header(const uint8_t *first, const uint8_t *second)
{
    for (int index = 0; index < HEADER_FIELDS; index++) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

static TrackOrderResult commit(TrackOrderJob *job)
{
    uint8_t bytes[TRACK_ORDER_HEADER_BYTES];
    uint8_t readback[TRACK_ORDER_HEADER_BYTES];
    TrackOrderConfig *config = &job->order->config;
    uint32_t offset = (uint32_t)job->bank * TRACK_ORDER_BANK_BYTES;
    make_header(job, bytes);
    (void)config->write(config->source.context, offset, bytes, sizeof bytes);
    if (!config->source.read(config->source.context, offset, readback, sizeof readback)) {
        return finish_job(job, TRACK_ORDER_UNCERTAIN);
    }
    if (!same_header(bytes, readback)) {
        return finish_job(job, header_prefix_valid(readback) ? TRACK_ORDER_UNCERTAIN
                                                             : TRACK_ORDER_FAILED);
    }
    job->order->bank = job->bank;
    job->order->generation = job->generation;
    job->order->count = job->count;
    job->order->valid = true;
    return finish_job(job, TRACK_ORDER_DONE);
}

TrackOrderResult track_order_job_step(TrackOrderJob *job)
{
    if (job->result != TRACK_ORDER_WORKING) {
        return job->result;
    }
    switch (job->stage) {
    case STAGE_INVALIDATE:
        return invalidate(job);
    case STAGE_WRITE:
        return write_block(job);
    case STAGE_VERIFY:
        return verify_block(job);
    case STAGE_COMMIT:
        return commit(job);
    }
    return finish_job(job, TRACK_ORDER_FAILED);
}

void track_order_job_abort(TrackOrderJob *job)
{
    if (job->result == TRACK_ORDER_WORKING) {
        (void)finish_job(job, TRACK_ORDER_FAILED);
    }
}
