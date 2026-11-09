#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"

enum {
    TRACK_ORDER_CAPACITY = UINT16_MAX,
    TRACK_ORDER_HEADER_BYTES = 512,
    TRACK_ORDER_BLOCK_TRACKS = 256,
    TRACK_ORDER_BANK_BYTES = 131584,
    TRACK_ORDER_FILE_BYTES = 2 * TRACK_ORDER_BANK_BYTES
};

typedef struct TrackOrderConfig {
    ByteSource source;
    bool (*write)(void *context, uint32_t offset, const void *bytes, uint32_t length);
    uint32_t size;
    uint32_t fingerprint;
    uint32_t track_limit;
} TrackOrderConfig;

typedef enum TrackOrderLoad {
    TRACK_ORDER_EMPTY,
    TRACK_ORDER_READY,
    TRACK_ORDER_UNREADABLE,
    TRACK_ORDER_UNSUPPORTED
} TrackOrderLoad;

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
    bool (*read)(const void *context, uint16_t first, uint16_t count, uint16_t *tracks);
    const void *context;
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

typedef enum TrackOrderResult {
    TRACK_ORDER_IDLE,
    TRACK_ORDER_WORKING,
    TRACK_ORDER_DONE,
    TRACK_ORDER_FAILED,
    TRACK_ORDER_UNCERTAIN
} TrackOrderResult;

typedef struct TrackOrderJob {
    TrackOrder *order;
    TrackOrderSource source;
    TrackOrderEdit edit;
    TrackOrderResult result;
    uint32_t generation;
    uint32_t hash;
    uint32_t verify_hash;
    uint32_t position;
    uint16_t count;
    uint8_t bank;
    uint8_t stage;
} TrackOrderJob;

TrackOrderLoad track_order_open(TrackOrder *order, TrackOrderConfig config);
bool track_order_read(const TrackOrder *order, uint16_t first, uint16_t count, uint16_t *tracks);
TrackOrderSource track_order_source(const TrackOrder *order);
bool track_order_job_begin(TrackOrderJob *job, TrackOrder *order, TrackOrderSource source,
                           TrackOrderEdit edit);
TrackOrderResult track_order_job_step(TrackOrderJob *job);
void track_order_job_abort(TrackOrderJob *job);
