#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_log.h"

enum {
    LISTENING_TEXT_BYTES = 128,
    LISTENING_RECORDING_BYTES = 37,
    LISTENING_PAD_LINE_BYTES = 64
};

extern const char LISTENING_EXPORT_PATH[];

typedef struct ListeningText {
    char artist[LISTENING_TEXT_BYTES];
    char album[LISTENING_TEXT_BYTES];
    char title[LISTENING_TEXT_BYTES];
    char recording[LISTENING_RECORDING_BYTES];
    uint16_t number;
} ListeningText;

typedef struct ListeningLookup {
    bool (*text)(void *context, const ListeningTrack *track, ListeningText *text);
    void *context;
} ListeningLookup;

typedef struct ListeningExport {
    ListeningCursor cursor;
    ListeningLookup lookup;
    StoreFile file;
    uint32_t bytes;
    uint32_t excess;
    uint32_t offset;
    uint32_t text_end;
    uint16_t fill;
    uint16_t exported;
    uint16_t unknown;
    uint16_t dropped;
    uint8_t stage;
    StoreResult result;
    uint8_t block[STORE_BLOCK_BYTES];
} ListeningExport;

void listening_export_begin(ListeningExport *job, const ListeningLog *log, ListeningLookup lookup);
bool listening_export_write(ListeningExport *job, StoreFile file);
StoreResult listening_export_step(ListeningExport *job);
