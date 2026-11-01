#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "library/playlist_reader.h"

enum {
    CUE_TEXT_CAPACITY = 256,
    CUE_LINE_CAPACITY = 1024
};

#define CUE_NO_START 0xFFFFFFFFu

typedef struct CueAlbum {
    char file[CUE_TEXT_CAPACITY];
    char title[CUE_TEXT_CAPACITY];
    char performer[CUE_TEXT_CAPACITY];
    char genre[CUE_TEXT_CAPACITY];
    char date[CUE_TEXT_CAPACITY];
} CueAlbum;

typedef struct CueEntry {
    uint32_t number;
    uint32_t start_ms;
    uint32_t end_ms;
    char title[CUE_TEXT_CAPACITY];
    char performer[CUE_TEXT_CAPACITY];
} CueEntry;

typedef struct CueSheet {
    PlaylistReader reader;
    char line[CUE_LINE_CAPACITY];
    CueAlbum album;
    CueEntry building;
    CueEntry pending;
    CueEntry out;
    bool has_building;
    bool has_pending;
    uint32_t files;
} CueSheet;

void cue_sheet_open(CueSheet *sheet, ByteSource source, uint32_t size);
const CueEntry *cue_sheet_next(CueSheet *sheet);
bool cue_sheet_usable(CueSheet *sheet);
