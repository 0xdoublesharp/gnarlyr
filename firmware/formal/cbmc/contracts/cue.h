#pragma once

#include "library/cue_sheet.h"
#include "playlist.h"

#define FORMAL_CUE_ENTRY_TEXTS(entry)                                                              \
    ((entry).title[CUE_TEXT_CAPACITY - 1] == '\0' &&                                               \
     (entry).performer[CUE_TEXT_CAPACITY - 1] == '\0')

#define FORMAL_CUE_TEXTS(sheet)                                                                    \
    ((sheet)->album.file[CUE_TEXT_CAPACITY - 1] == '\0' &&                                         \
     (sheet)->album.title[CUE_TEXT_CAPACITY - 1] == '\0' &&                                        \
     (sheet)->album.performer[CUE_TEXT_CAPACITY - 1] == '\0' &&                                    \
     (sheet)->album.genre[CUE_TEXT_CAPACITY - 1] == '\0' &&                                        \
     (sheet)->album.date[CUE_TEXT_CAPACITY - 1] == '\0' &&                                         \
     FORMAL_CUE_ENTRY_TEXTS((sheet)->building) && FORMAL_CUE_ENTRY_TEXTS((sheet)->pending) &&      \
     FORMAL_CUE_ENTRY_TEXTS((sheet)->out))

#define FORMAL_CUE_RANK(sheet) (((sheet)->has_building ? 2 : 0) + ((sheet)->has_pending ? 1 : 0))

#define FORMAL_CUE_STATE(sheet)                                                                    \
    (sheet)->album, (sheet)->building, (sheet)->pending, (sheet)->out, (sheet)->has_building,      \
        (sheet)->has_pending, (sheet)->files

#define FORMAL_CUE_READER_STATE(sheet)                                                             \
    (sheet)->reader.offset, (sheet)->reader.start_ms, FORMAL_PLAYLIST_CACHE(&(sheet)->reader)

#define FORMAL_CUE_SHEET(sheet)                                                                    \
    __CPROVER_requires(__CPROVER_is_fresh(sheet, sizeof(CueSheet)))                                \
        __CPROVER_requires(FORMAL_PLAYLIST_GOOD(&(sheet)->reader) &&                               \
                           FORMAL_PLAYLIST_NO_WRAP(&(sheet)->reader) &&                            \
                           (uint64_t)(sheet)->reader.offset <= (uint64_t)(sheet)->reader.size + 3) \
            __CPROVER_requires(FORMAL_CUE_TEXTS(sheet))

#define FORMAL_CUE_KEPT(sheet)                                                                     \
    __CPROVER_ensures(FORMAL_CUE_TEXTS(sheet))                                                     \
        __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(&(sheet)->reader) &&                                \
                          (uint64_t)(sheet)->reader.offset <= (uint64_t)(sheet)->reader.size + 3)

bool formal_cue_reader_next(PlaylistReader *reader, char *entry, uint32_t capacity)
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(CueSheet)))
        __CPROVER_requires(FORMAL_PLAYLIST_GOOD(reader)) FORMAL_PLAYLIST_NEXT_REQUIRES
    __CPROVER_requires(__CPROVER_pointer_in_range_dfcc(&((CueSheet *)reader)->line[0], entry,
                                                       &((CueSheet *)reader)->line[0]) &&
                       capacity == CUE_LINE_CAPACITY)
        __CPROVER_assigns(reader->offset, reader->start_ms, FORMAL_PLAYLIST_CACHE(reader),
                          ((CueSheet *)reader)->line) FORMAL_PLAYLIST_NEXT_ENSURES;

#define FORMAL_CONTRACT_handle_line                                                                \
    __CPROVER_requires(__CPROVER_is_fresh(sheet, sizeof(CueSheet))) __CPROVER_requires(            \
        __CPROVER_pointer_in_range_dfcc(&sheet->line[0], line, &sheet->line[0]))                   \
        __CPROVER_requires(FORMAL_TEXT_ENDS(sheet->line, CUE_LINE_CAPACITY))                       \
            __CPROVER_requires(FORMAL_CUE_TEXTS(sheet))                                            \
                __CPROVER_assigns(sheet->line, FORMAL_CUE_STATE(sheet))                            \
                    __CPROVER_ensures(FORMAL_CUE_TEXTS(sheet)) __CPROVER_ensures(                  \
                        __CPROVER_return_value == NULL || __CPROVER_return_value == &sheet->out)

#define FORMAL_CONTRACT_cue_sheet_next                                                             \
    FORMAL_CUE_SHEET(sheet)                                                                        \
    __CPROVER_assigns(sheet->line, FORMAL_CUE_STATE(sheet),                                        \
                      FORMAL_CUE_READER_STATE(sheet)) FORMAL_CUE_KEPT(sheet)                       \
        __CPROVER_ensures(__CPROVER_return_value == NULL || __CPROVER_return_value == &sheet->out) \
            __CPROVER_ensures(__CPROVER_return_value != NULL ||                                    \
                              (!sheet->has_pending && sheet->reader.offset >= sheet->reader.size)) \
                __CPROVER_ensures(sheet->reader.offset >= __CPROVER_old(sheet->reader.offset))     \
                    __CPROVER_ensures(                                                             \
                        sheet->reader.offset > __CPROVER_old(sheet->reader.offset) ||              \
                        __CPROVER_return_value == NULL ||                                          \
                        FORMAL_CUE_RANK(sheet) < (__CPROVER_old(sheet->has_building) ? 2 : 0) +    \
                                                     (__CPROVER_old(sheet->has_pending) ? 1 : 0))

#define FORMAL_LOOP_cue_sheet_next_0                                                               \
    __CPROVER_assigns(sheet->line, FORMAL_CUE_STATE(sheet), FORMAL_CUE_READER_STATE(sheet))        \
        __CPROVER_loop_invariant(                                                                  \
            FORMAL_CUE_TEXTS(sheet) && FORMAL_PLAYLIST_GOOD(&sheet->reader) &&                     \
            (uint64_t)sheet->reader.offset <= (uint64_t)sheet->reader.size + 3 &&                  \
            sheet->reader.offset >= __CPROVER_loop_entry(sheet->reader.offset) &&                  \
            (sheet->reader.offset > __CPROVER_loop_entry(sheet->reader.offset) ||                  \
             (sheet->has_building == __CPROVER_loop_entry(sheet->has_building) &&                  \
              sheet->has_pending == __CPROVER_loop_entry(sheet->has_pending))))                    \
            __CPROVER_decreases((int64_t)sheet->reader.size + 4 - (int64_t)sheet->reader.offset)

#define FORMAL_CONTRACT_cue_sheet_usable                                                           \
    FORMAL_CUE_SHEET(sheet)                                                                        \
    __CPROVER_assigns(sheet->line, FORMAL_CUE_STATE(sheet), FORMAL_CUE_READER_STATE(sheet))        \
        FORMAL_CUE_KEPT(sheet)

#define FORMAL_LOOP_cue_sheet_usable_0                                                             \
    __CPROVER_assigns(tracks, sheet->line, FORMAL_CUE_STATE(sheet),                                \
                      FORMAL_CUE_READER_STATE(sheet))                                              \
        __CPROVER_loop_invariant(                                                                  \
            FORMAL_CUE_TEXTS(sheet) && FORMAL_PLAYLIST_GOOD(&sheet->reader) &&                     \
            (uint64_t)sheet->reader.offset <= (uint64_t)sheet->reader.size + 3)                    \
            __CPROVER_decreases((int64_t)sheet->reader.size + 4 - (int64_t)sheet->reader.offset,   \
                                FORMAL_CUE_RANK(sheet))

#define FORMAL_CONTRACT_cue_sheet_open                                                             \
    __CPROVER_requires(__CPROVER_is_fresh(sheet, sizeof(CueSheet)))                                \
        __CPROVER_requires(source.read == formal_card_read)                                        \
            __CPROVER_assigns(sheet->reader, sheet->album.file[0], sheet->album.title[0],          \
                              sheet->album.performer[0], sheet->album.genre[0],                    \
                              sheet->album.date[0], sheet->has_building, sheet->has_pending,       \
                              sheet->files)                                                        \
                __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(&sheet->reader) &&                          \
                                  sheet->reader.size == size && sheet->reader.offset <= 3 &&       \
                                  !sheet->has_building && !sheet->has_pending)
