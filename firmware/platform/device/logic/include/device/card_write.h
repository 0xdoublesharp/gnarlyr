#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    CARD_READ_SINGLE_BLOCK = 17,
    CARD_READ_MULTIPLE_BLOCK = 18,
    CARD_WRITE_SINGLE_BLOCK = 24,
    CARD_WRITE_MULTIPLE_BLOCK = 25
};

typedef struct CardWriter {
    bool (*write_data)(void *context, uint8_t command, uint32_t address, uint32_t count,
                       const uint32_t *words);
    bool (*stop)(void *context);
    bool (*wait_ready)(void *context);
    void *context;
} CardWriter;

bool card_write(const CardWriter *writer, uint32_t address, uint32_t count, const uint32_t *words);
bool card_write_start(const CardWriter *writer, uint32_t address, uint32_t count,
                      const uint32_t *words);
bool card_write_finish(const CardWriter *writer);

typedef struct CardStreamOps {
    bool (*pre_count)(void *context, uint32_t count);
    bool (*begin)(void *context, uint8_t command, uint32_t address, uint32_t count);
    bool (*feed)(void *context, uint32_t *words, uint32_t count, uint32_t *accepted);
    bool (*end)(void *context);
    bool (*stop)(void *context);
    bool (*wait_ready)(void *context);
    void *context;
    bool counted;
} CardStreamOps;

typedef struct CardStreamStart {
    uint32_t address;
    uint32_t count;
    bool read;
} CardStreamStart;

typedef struct CardStream {
    const CardStreamOps *ops;
    uint32_t count;
    bool closed_ended;
    bool failed;
} CardStream;

bool card_stream_open(CardStream *stream, const CardStreamOps *ops, CardStreamStart start);
bool card_stream_feed(CardStream *stream, uint32_t *words, uint32_t count, uint32_t *accepted);
bool card_stream_close(CardStream *stream, bool complete);
