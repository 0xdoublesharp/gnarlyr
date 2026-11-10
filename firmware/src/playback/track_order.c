#include "playback/track_order.h"

#include <stddef.h>

const char TRACK_ORDER_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/order.bin";

static const StoreKind KIND = {.magic = {'D', 'S', 'T', 'O'}, .version = 1};

typedef struct Request {
    const TrackOrderConfig *config;
    const uint32_t *generation;
} Request;

static StoreSlot slot_of(const TrackOrderConfig *config, uint8_t bank)
{
    return (StoreSlot){
        .file = &config->file, .kind = &KIND, .offset = (uint32_t)bank * TRACK_ORDER_BANK_BYTES};
}

static uint32_t data_offset(uint8_t bank, uint32_t position)
{
    return (uint32_t)bank * TRACK_ORDER_BANK_BYTES + STORE_BLOCK_BYTES + 2 * position;
}

static uint16_t block_tracks(uint32_t remaining)
{
    return (uint16_t)(remaining > TRACK_ORDER_BLOCK_TRACKS ? TRACK_ORDER_BLOCK_TRACKS : remaining);
}

static bool config_valid(const TrackOrderConfig *config)
{
    return config->file.source.read != NULL && config->file.size >= TRACK_ORDER_FILE_BYTES &&
           config->track_limit <= TRACK_ORDER_CAPACITY;
}

static bool ids_valid(const TrackOrderConfig *config, const uint8_t *bytes, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        if (store_get16(bytes + 2 * index) >= config->track_limit) {
            return false;
        }
    }
    return true;
}

static StoreLoad check_payload(const TrackOrderConfig *config, uint8_t bank,
                               const StoreHeader *header)
{
    uint8_t block[STORE_BLOCK_BYTES];
    StoreSlot slot = slot_of(config, bank);
    StoreScan scan = store_scan_begin();
    while (scan.position < header->bytes) {
        uint32_t count = store_scan_chunk(&scan, header->bytes);
        if (!store_scan_next(&slot, &scan, header->bytes, block)) {
            return STORE_UNREADABLE;
        }
        if (!ids_valid(config, block, count / 2)) {
            return STORE_CORRUPT;
        }
    }
    return scan.hash == header->hash ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad check_bank(void *context, uint8_t bank, const StoreHeader *header)
{
    const Request *request = context;
    bool wanted = request->generation == NULL || header->generation == *request->generation;
    bool mine = header->binding == request->config->fingerprint &&
                header->bytes == 2u * header->count && wanted;
    return mine ? check_payload(request->config, bank, header) : STORE_EMPTY;
}

static StoreLoad open_matching(TrackOrder *order, TrackOrderConfig config,
                               const uint32_t *generation)
{
    *order = (TrackOrder){.config = config};
    Request request = {.config = &order->config, .generation = generation};
    StorePair pair = {.file = &order->config.file,
                      .kind = &KIND,
                      .bank_bytes = TRACK_ORDER_BANK_BYTES,
                      .check = check_bank,
                      .context = &request};
    StoreChoice choice;
    StoreLoad load = config_valid(&config) ? store_pair_open(&pair, &choice) : STORE_UNREADABLE;
    order->needs_reload = store_blocking(load);
    if (load != STORE_READY) {
        return order->needs_reload ? load : STORE_EMPTY;
    }
    order->generation = choice.header.generation;
    order->count = choice.header.count;
    order->bank = choice.bank;
    order->valid = true;
    return STORE_READY;
}

StoreLoad track_order_open(TrackOrder *order, TrackOrderConfig config)
{
    return open_matching(order, config, NULL);
}

StoreLoad track_order_open_at(TrackOrder *order, TrackOrderConfig config, uint32_t generation)
{
    return open_matching(order, config, &generation);
}

static bool read_ids(const TrackOrder *order, uint32_t position, uint16_t count, uint16_t *tracks)
{
    uint8_t *bytes = (uint8_t *)tracks;
    if (!store_file_read(&order->config.file, data_offset(order->bank, position), bytes,
                         2u * count)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        uint16_t track = store_get16(bytes + 2u * index);
        if (track >= order->config.track_limit) {
            return false;
        }
        tracks[index] = track;
    }
    return true;
}

bool track_order_read(const TrackOrder *order, uint16_t first, uint16_t count, uint16_t *tracks)
{
    if (!order->valid || first > order->count || count > order->count - first ||
        (count > 0 && tracks == NULL)) {
        return false;
    }
    for (uint32_t done = 0; done < count;) {
        uint16_t run = block_tracks(count - done);
        if (!read_ids(order, first + done, run, tracks + done)) {
            return false;
        }
        done += run;
    }
    return true;
}

static bool source_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    return track_order_read(context, first, count, tracks);
}

TrackOrderSource track_order_source(TrackOrder *order)
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

static bool writable(const TrackOrder *order)
{
    return !order->busy && !order->needs_reload && config_valid(&order->config) &&
           order->config.file.write != NULL;
}

bool track_order_job_begin(TrackOrderJob *job, TrackOrder *order, TrackOrderSource source,
                           TrackOrderEdit edit)
{
    uint16_t count = 0;
    if (!writable(order) || !edit_count(source, edit, &count) ||
        (edit.operation == TRACK_ORDER_INSERT && edit.track >= order->config.track_limit)) {
        return false;
    }
    *job = (TrackOrderJob){.order = order,
                           .source = source,
                           .edit = edit,
                           .write.header = {.generation = order->valid ? order->generation + 1 : 1,
                                            .bytes = 2u * count,
                                            .binding = order->config.fingerprint,
                                            .count = count},
                           .bank = order->valid ? (uint8_t)(1 - order->bank) : 0};
    store_write_begin(&job->write, order->valid ? &order->generation : NULL);
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

static bool read_edited(const TrackOrderJob *job, uint32_t first, uint16_t count, uint16_t *tracks)
{
    for (uint16_t position = 0; position < count;) {
        uint32_t source = source_index(job, first + position);
        if (source == UINT32_MAX) {
            tracks[position++] = job->edit.track;
            continue;
        }
        uint16_t run = run_length(job, first + position, (uint16_t)(count - position));
        if (!job->source.read(job->source.context, (uint16_t)source, run, tracks + position)) {
            return false;
        }
        position = (uint16_t)(position + run);
    }
    return true;
}

static bool fill_ids(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    const TrackOrderJob *job = context;
    uint16_t tracks = (uint16_t)(count / 2);
    if (!read_edited(job, position / 2, tracks, block->words)) {
        return false;
    }
    for (uint16_t index = 0; index < tracks; index++) {
        uint16_t track = block->words[index];
        if (track >= job->order->config.track_limit) {
            return false;
        }
        store_put16(block->bytes + 2u * index, track);
    }
    return true;
}

static void adopt(TrackOrder *order, const TrackOrderJob *job)
{
    order->bank = job->bank;
    order->generation = job->write.header.generation;
    order->count = job->write.header.count;
    order->valid = true;
}

StoreResult track_order_job_step(TrackOrderJob *job)
{
    if (job->write.result != STORE_WORKING) {
        return job->write.result;
    }
    TrackOrder *order = job->order;
    StoreSlot slot = slot_of(&order->config, job->bank);
    StoreResult result = store_write_step(&job->write, &slot, (StoreFill){fill_ids, job});
    if (result == STORE_WORKING) {
        return result;
    }
    order->busy = false;
    order->needs_reload |= result == STORE_UNCERTAIN;
    if (result == STORE_DONE) {
        adopt(order, job);
    }
    return result;
}

void track_order_job_abort(TrackOrderJob *job)
{
    if (job->write.result == STORE_WORKING) {
        store_write_abort(&job->write);
        job->order->busy = false;
    }
}

uint8_t track_order_job_progress(const TrackOrderJob *job)
{
    return store_write_progress(&job->write);
}
