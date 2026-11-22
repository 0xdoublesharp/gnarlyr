#pragma once

#include "formal.h"
#include "library/playlist_reader.h"

#define FORMAL_PLAYLIST_GOOD(reader)                                                               \
    ((reader)->source.read == formal_card_read && (reader)->chunk_length <= PLAYLIST_CHUNK)

#define FORMAL_PLAYLIST_NO_WRAP(reader) (!(reader)->utf8 || (reader)->size <= 0xFFFFFFFCu)

#define FORMAL_PLAYLIST_READER(reader)                                                             \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(PlaylistReader)))                         \
        __CPROVER_requires(FORMAL_PLAYLIST_GOOD(reader))

#define FORMAL_PLAYLIST_CACHE(reader) (reader)->chunk, (reader)->chunk_start, (reader)->chunk_length

#define FORMAL_LINE_GOOD(line)                                                                     \
    ((line)->capacity >= 1 && (line)->capacity <= FORMAL_OBJECT_LIMIT &&                           \
     (line)->length < (line)->capacity && (line)->content_end <= (line)->length)

#define FORMAL_CONTRACT_byte_at                                                                    \
    FORMAL_PLAYLIST_READER(reader)                                                                 \
    __CPROVER_requires(__CPROVER_is_fresh(value, 1))                                               \
        __CPROVER_assigns(FORMAL_PLAYLIST_CACHE(reader), *value)                                   \
            __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader))                                        \
                __CPROVER_ensures(!__CPROVER_return_value || offset < reader->size)

#define FORMAL_CONTRACT_valid_sequence                                                             \
    FORMAL_PLAYLIST_READER(reader)                                                                 \
    __CPROVER_requires(length <= 4 && offset < reader->size)                                       \
        __CPROVER_assigns(FORMAL_PLAYLIST_CACHE(reader))                                           \
            __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader))                                        \
                __CPROVER_ensures(!__CPROVER_return_value || length <= 1 ||                        \
                                  (uint64_t)offset + length - 1 < reader->size)

#define FORMAL_CONTRACT_is_utf8                                                                    \
    FORMAL_PLAYLIST_READER(reader)                                                                 \
    __CPROVER_assigns(FORMAL_PLAYLIST_CACHE(reader)) __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader))

#define FORMAL_LOOP_is_utf8_0                                                                      \
    __CPROVER_assigns(offset, lead, FORMAL_PLAYLIST_CACHE(reader))                                 \
        __CPROVER_loop_invariant(FORMAL_PLAYLIST_GOOD(reader) && offset <= reader->size)           \
            __CPROVER_decreases((int64_t)reader->size - (int64_t)offset)

#define FORMAL_CONTRACT_playlist_reader_open                                                       \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(PlaylistReader)))                         \
        __CPROVER_requires(source.read == formal_card_read) __CPROVER_assigns(*reader)             \
            __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader) && reader->size == size &&              \
                              reader->offset <= 3)

#define FORMAL_CONTRACT_next_code_point                                                            \
    FORMAL_PLAYLIST_READER(reader)                                                                 \
    __CPROVER_assigns(reader->offset, FORMAL_PLAYLIST_CACHE(reader))                               \
        __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader)) __CPROVER_ensures(                         \
            reader->offset - __CPROVER_old(reader->offset) >= 1 &&                                 \
            reader->offset - __CPROVER_old(reader->offset) <= (reader->utf8 ? 4u : 1u))

#define FORMAL_CONTRACT_put                                                                        \
    __CPROVER_requires(__CPROVER_is_fresh(line, sizeof(Line))) __CPROVER_requires(                 \
        FORMAL_LINE_GOOD(line) && __CPROVER_is_fresh(line->text, line->capacity))                  \
        __CPROVER_assigns(line->length, line->content_end,                                         \
                          __CPROVER_object_upto(line->text, line->capacity))                       \
            __CPROVER_ensures(FORMAL_LINE_GOOD(line))

#define FORMAL_CONTRACT_read_line                                                                  \
    FORMAL_PLAYLIST_READER(reader)                                                                 \
    __CPROVER_requires(FORMAL_PLAYLIST_NO_WRAP(reader))                                            \
        __CPROVER_requires(__CPROVER_is_fresh(line, sizeof(Line))) __CPROVER_requires(             \
            FORMAL_LINE_GOOD(line) && __CPROVER_is_fresh(line->text, line->capacity))              \
            __CPROVER_assigns(reader->offset, FORMAL_PLAYLIST_CACHE(reader), line->length,         \
                              line->content_end,                                                   \
                              __CPROVER_object_upto(line->text, line->capacity))                   \
                __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader) && FORMAL_LINE_GOOD(line))          \
                    __CPROVER_ensures(!__CPROVER_return_value ||                                   \
                                      (line->text[line->content_end] == '\0' &&                    \
                                       __CPROVER_old(reader->offset) < reader->size &&             \
                                       reader->offset > __CPROVER_old(reader->offset)))            \
                        __CPROVER_ensures(__CPROVER_return_value ||                                \
                                          (reader->offset == __CPROVER_old(reader->offset) &&      \
                                           reader->offset >= reader->size))                        \
                            __CPROVER_ensures((uint64_t)reader->offset <=                          \
                                                  (uint64_t)reader->size + 3 ||                    \
                                              reader->offset == __CPROVER_old(reader->offset))

#define FORMAL_LOOP_read_line_0                                                                    \
    __CPROVER_assigns(reader->offset, FORMAL_PLAYLIST_CACHE(reader), line->length,                 \
                      line->content_end, __CPROVER_object_upto(line->text, line->capacity))        \
        __CPROVER_loop_invariant(FORMAL_PLAYLIST_GOOD(reader) && FORMAL_LINE_GOOD(line) &&         \
                                 reader->offset >= __CPROVER_loop_entry(reader->offset) &&         \
                                 (uint64_t)reader->offset <= (uint64_t)reader->size + 3)           \
            __CPROVER_decreases((int64_t)reader->size - (int64_t)reader->offset)

#define FORMAL_PLAYLIST_NEXT_REQUIRES                                                              \
    __CPROVER_requires(FORMAL_PLAYLIST_NO_WRAP(reader))                                            \
        __CPROVER_requires((uint64_t)reader->offset <= (uint64_t)reader->size + 3)

#define FORMAL_ENTRY_ENDS(entry)                                                                   \
    __CPROVER_exists                                                                               \
    {                                                                                              \
        uint32_t formal_at;                                                                        \
        formal_at < PLAYLIST_ENTRY_CAPACITY && (entry)[formal_at] == '\0'                          \
    }

#define FORMAL_PLAYLIST_NEXT_ENSURES                                                               \
    __CPROVER_ensures(FORMAL_PLAYLIST_GOOD(reader) &&                                              \
                      (uint64_t)reader->offset <= (uint64_t)reader->size + 3)                      \
        __CPROVER_ensures(reader->offset >= __CPROVER_old(reader->offset)) __CPROVER_ensures(      \
            !__CPROVER_return_value ||                                                             \
            (FORMAL_ENTRY_ENDS(entry) && reader->offset > __CPROVER_old(reader->offset)))          \
            __CPROVER_ensures(__CPROVER_return_value || reader->offset >= reader->size)

#define FORMAL_CONTRACT_playlist_reader_next                                                       \
    FORMAL_PLAYLIST_READER(reader)                                                                 \
    FORMAL_PLAYLIST_NEXT_REQUIRES                                                                  \
    __CPROVER_requires(capacity == PLAYLIST_ENTRY_CAPACITY)                                        \
        __CPROVER_requires(__CPROVER_is_fresh(entry, capacity))                                    \
            __CPROVER_assigns(reader->offset, reader->start_ms, FORMAL_PLAYLIST_CACHE(reader),     \
                              __CPROVER_object_upto(entry, capacity)) FORMAL_PLAYLIST_NEXT_ENSURES

#define FORMAL_LOOP_playlist_reader_next_0                                                         \
    __CPROVER_assigns(reader->offset, reader->start_ms, FORMAL_PLAYLIST_CACHE(reader), line,       \
                      __CPROVER_object_upto(entry, capacity))                                      \
        __CPROVER_loop_invariant(FORMAL_PLAYLIST_GOOD(reader) && line.text == entry &&             \
                                 line.capacity == capacity && line.length < capacity &&            \
                                 line.content_end <= line.length &&                                \
                                 reader->offset >= __CPROVER_loop_entry(reader->offset) &&         \
                                 (uint64_t)reader->offset <= (uint64_t)reader->size + 3)           \
            __CPROVER_decreases((int64_t)reader->size + 4 - (int64_t)reader->offset)
