#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/cue_sheet.h"
#include "library/library_builder.h"
#include "library/library_scan.h"
#include "library/playlist_reader.h"
#include "library/track_fields.h"
#include "library/track_tags.h"
#include "mp4_chapters.h"

enum {
    ENTRY_PLAIN = 0,
    ENTRY_DIRECTORY = 1,
    ENTRY_CLAIMED = 2
};

typedef struct Level {
    uint32_t start;
    uint32_t first;
    uint32_t count;
    uint32_t next;
    uint32_t path_length;
    bool partial;
} Level;

typedef struct ScanState {
    LibraryBuilder builder;
    ScanTrackFilter filter;
    uint32_t hidden;
    TrackTags tags;
    TrackFields fields;
    ScanEntry entry;
    Level levels[SCAN_MAX_DEPTH];
    uint32_t depth;
    char path[SCAN_PATH_CAPACITY + 2];
    uint32_t path_length;
    uint8_t *listing;
    uint32_t listing_size;
    uint32_t listing_used;
    uint32_t playlist_floor;
    uint32_t playlist_count;
    PlaylistReader playlist;
    CueSheet cue;
    Mp4Chapters chapters;
    char sheet_name[SCAN_NAME_CAPACITY];
    char audio_path[SCAN_PATH_CAPACITY + 2];
    char entry_text[PLAYLIST_ENTRY_CAPACITY];
    char playlist_name[SCAN_NAME_CAPACITY];
    char cutoff[SCAN_NAME_CAPACITY];
} ScanState;

const char *scan_entry_name(const ScanState *state, uint32_t offset);
uint32_t scan_text_length(const char *text);
bool scan_append_path(ScanState *state, const char *name, bool directory);
void scan_truncate_path(ScanState *state, uint32_t length);
ScanResult scan_builder_result(BuilderResult result);
ScanResult scan_add_track(ScanState *state, const BuilderTrack *track);
void scan_report(const ScanJob *job, const ScanStats *stats);
bool scan_has_suffix(const char *name, const char *suffix);
ScanResult scan_mark_cue_targets(ScanState *state, const ScanFileSystem *files, const Level *level);
ScanResult scan_add_chapters(ScanState *state, const ScanJob *job, ScanStats *stats);
ScanResult scan_add_cue(ScanState *state, const ScanJob *job, ScanStats *stats, uint32_t offset);
