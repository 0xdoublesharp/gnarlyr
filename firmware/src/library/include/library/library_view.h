#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library_browse.h"
#include "library/library_builder.h"

enum {
    LIBRARY_VIEW_TEXT_BYTES = 64,
    LIBRARY_VIEW_WORK_BYTES = 262144,
    LIBRARY_VIEW_PAGE_TRACKS = 256,
    LIBRARY_VIEW_PRIVATE_BYTES = 3072,
    LIBRARY_VIEW_FIELD_TITLE = 1u << 0,
    LIBRARY_VIEW_FIELD_ARTIST = 1u << 1,
    LIBRARY_VIEW_FIELD_ALBUM = 1u << 2,
    LIBRARY_VIEW_FIELD_ALBUM_ARTIST = 1u << 3,
    LIBRARY_VIEW_FIELD_GENRE = 1u << 4,
    LIBRARY_VIEW_FIELD_PATH = 1u << 5,
    LIBRARY_VIEW_FIELDS_ALL = (1u << 6) - 1u
};

typedef enum LibraryViewSort {
    LIBRARY_VIEW_SORT_ORIGINAL,
    LIBRARY_VIEW_SORT_TITLE,
    LIBRARY_VIEW_SORT_ARTIST,
    LIBRARY_VIEW_SORT_ALBUM,
    LIBRARY_VIEW_SORT_YEAR,
    LIBRARY_VIEW_SORT_DURATION
} LibraryViewSort;

typedef enum LibraryViewAcceptance {
    LIBRARY_VIEW_ACCEPT,
    LIBRARY_VIEW_REJECT,
    LIBRARY_VIEW_ACCEPT_FAILED
} LibraryViewAcceptance;

typedef enum LibraryViewState {
    LIBRARY_VIEW_IDLE,
    LIBRARY_VIEW_SCANNING,
    LIBRARY_VIEW_SORTING,
    LIBRARY_VIEW_READY,
    LIBRARY_VIEW_CANCELLED,
    LIBRARY_VIEW_READ_FAILED,
    LIBRARY_VIEW_WRITE_FAILED,
    LIBRARY_VIEW_INVALID,
    LIBRARY_VIEW_CHANGED
} LibraryViewState;

typedef struct LibraryViewConfig {
    BrowseQuery source;
    char text[LIBRARY_VIEW_TEXT_BYTES];
    uint32_t fields;
    LibraryViewSort sort;
    bool descending;
    uint16_t artist;
    uint16_t genre;
    uint16_t album;
    uint16_t year_min;
    uint16_t year_max;
    uint32_t duration_min_ms;
    uint32_t duration_max_ms;
    LibraryViewAcceptance (*accept)(void *context, uint16_t track, const LibraryTrack *record);
    void *context;
} LibraryViewConfig;

typedef struct LibraryView {
    const Library *library;
    LibraryViewConfig config;
    BuilderScratch work;
    LibraryViewState state;
    uint32_t scanned;
    uint32_t total;
    uint16_t count;
    uint32_t fingerprint;
    union {
        uint32_t alignment;
        uint8_t bytes[LIBRARY_VIEW_PRIVATE_BYTES];
    } private;
} LibraryView;

LibraryViewConfig library_view_defaults(void);
LibraryViewState library_view_begin(LibraryView *view, const Library *library,
                                    const LibraryViewConfig *config, BuilderScratch work);
LibraryViewState library_view_step(LibraryView *view);
void library_view_cancel(LibraryView *view);
bool library_view_read(const LibraryView *view, uint16_t first, uint16_t count, uint16_t *tracks);
bool library_view_source_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks);
