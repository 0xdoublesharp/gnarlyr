#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/store.h"

enum {
    TRACK_ORDER_CAPACITY = UINT16_MAX,
    TRACK_ORDER_BLOCK_TRACKS = STORE_BLOCK_BYTES / 2,
    TRACK_ORDER_BANK_BYTES = 131584,
    TRACK_ORDER_FILE_BYTES = 2 * TRACK_ORDER_BANK_BYTES
};

extern const char TRACK_ORDER_PATH[];

typedef struct TrackOrderConfig {
    StoreFile file;
    uint32_t fingerprint;
    uint32_t track_limit;
} TrackOrderConfig;

typedef struct TrackOrder {
    TrackOrderConfig config;
    uint32_t generation;
    uint16_t count;
    uint8_t bank;
    bool valid;
    bool busy;
    bool needs_reload;
} TrackOrder;

typedef struct TrackOrderSource {
    bool (*read)(void *context, uint16_t first, uint16_t count, uint16_t *tracks);
    void *context;
    uint16_t count;
} TrackOrderSource;

typedef enum TrackOrderOperation {
    TRACK_ORDER_COPY,
    TRACK_ORDER_MOVE,
    TRACK_ORDER_REMOVE,
    TRACK_ORDER_INSERT,
    TRACK_ORDER_CLEAR
} TrackOrderOperation;

typedef struct TrackOrderEdit {
    TrackOrderOperation operation;
    uint16_t from;
    uint16_t to;
    uint16_t track;
} TrackOrderEdit;

typedef struct TrackOrderJob {
    TrackOrder *order;
    TrackOrderSource source;
    TrackOrderEdit edit;
    StoreWrite write;
    uint8_t bank;
} TrackOrderJob;

StoreLoad track_order_open(TrackOrder *order, TrackOrderConfig config);
StoreLoad track_order_open_at(TrackOrder *order, TrackOrderConfig config, uint32_t generation);
bool track_order_read(const TrackOrder *order, uint16_t first, uint16_t count, uint16_t *tracks);
TrackOrderSource track_order_source(TrackOrder *order);
bool track_order_job_begin(TrackOrderJob *job, TrackOrder *order, TrackOrderSource source,
                           TrackOrderEdit edit);
StoreResult track_order_job_step(TrackOrderJob *job);
void track_order_job_abort(TrackOrderJob *job);
uint8_t track_order_job_progress(const TrackOrderJob *job);
