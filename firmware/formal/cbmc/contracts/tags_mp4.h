#pragma once

#include "tags.h"

#define FORMAL_MP4_READER(reader)                                                                  \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(__CPROVER_is_fresh((reader)->tags, sizeof(TrackTags)))                  \
            __CPROVER_requires((reader)->source.read == formal_card_read &&                        \
                               (reader)->unsync == NULL && (reader)->source.context == NULL)       \
                __CPROVER_requires(FORMAL_TAGS_GOOD(reader))

#define FORMAL_MP4_ATOM(atom, reader) ((atom)->body <= (atom)->end && (atom)->end <= (reader)->size)

#define FORMAL_MP4_PARENT(atom, reader)                                                            \
    __CPROVER_requires(__CPROVER_is_fresh(atom, sizeof(Mp4Atom)) && FORMAL_MP4_ATOM(atom, reader))

#define FORMAL_MP4_FRAME(reader)                                                                   \
    (reader)->present, (reader)->replay_gain, __CPROVER_object_whole((reader)->tags)

#define FORMAL_CONTRACT_tags_mp4_read_atom                                                         \
    FORMAL_MP4_READER(reader)                                                                      \
    __CPROVER_requires(limit <= reader->size) __CPROVER_requires(                                  \
        __CPROVER_is_fresh(atom, sizeof(Mp4Atom))) __CPROVER_assigns(*atom)                        \
        __CPROVER_ensures(!__CPROVER_return_value ||                                               \
                          (offset <= limit && limit - offset >= ATOM_HEADER &&                     \
                           atom->offset == offset && atom->body - offset >= ATOM_HEADER &&         \
                           atom->body >= offset && atom->body <= atom->end && atom->end <= limit))

#define FORMAL_CONTRACT_tags_mp4_find_child                                                        \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(parent, reader)                                                              \
    __CPROVER_requires(__CPROVER_is_fresh(type, 5))                                                \
        __CPROVER_requires(__CPROVER_is_fresh(found, sizeof(Mp4Atom))) __CPROVER_assigns(*found)   \
            __CPROVER_ensures(!__CPROVER_return_value ||                                           \
                              (found->body <= found->end && found->end <= parent->end))

#define FORMAL_LOOP_tags_mp4_find_child_0                                                          \
    __CPROVER_assigns(cursor, atom)                                                                \
        __CPROVER_loop_invariant(cursor == __CPROVER_loop_entry(cursor) || cursor <= parent->end)  \
            __CPROVER_decreases((int64_t)parent->end - (int64_t)cursor)

#define FORMAL_CONTRACT_sound_track                                                                \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(moov, reader)                                                                \
    __CPROVER_requires(__CPROVER_is_fresh(trak, sizeof(Mp4Atom))) __CPROVER_assigns(*trak)         \
        __CPROVER_ensures(!__CPROVER_return_value || FORMAL_MP4_ATOM(trak, reader))

#define FORMAL_LOOP_sound_track_0                                                                  \
    __CPROVER_assigns(cursor, atom)                                                                \
        __CPROVER_loop_invariant(cursor == __CPROVER_loop_entry(cursor) || cursor <= moov->end)    \
            __CPROVER_decreases((int64_t)moov->end - (int64_t)cursor)

#define FORMAL_CONTRACT_read_duration                                                              \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(trak, reader)                                                                \
    __CPROVER_assigns(reader->tags->duration_ms) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_read_gain_item                                                             \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(item, reader)                                                                \
    __CPROVER_assigns(reader->replay_gain) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_LOOP_read_gain_item_0                                                               \
    __CPROVER_assigns(cursor, data, reader->replay_gain)                                           \
        __CPROVER_loop_invariant(cursor == __CPROVER_loop_entry(cursor) || cursor <= item->end)    \
            __CPROVER_decreases((int64_t)item->end - (int64_t)cursor)

#define FORMAL_CONTRACT_read_item                                                                  \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(item_atom, reader)                                                           \
    __CPROVER_assigns(FORMAL_MP4_FRAME(reader)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_LOOP_read_item_0                                                                    \
    __CPROVER_assigns(cursor, data, FORMAL_MP4_FRAME(reader)) __CPROVER_loop_invariant(            \
        FORMAL_TAGS_GOOD(reader) &&                                                                \
        (cursor == __CPROVER_loop_entry(cursor) || cursor <= item_atom->end))                      \
        __CPROVER_decreases((int64_t)item_atom->end - (int64_t)cursor)

#define FORMAL_CONTRACT_read_items                                                                 \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(ilst, reader)                                                                \
    __CPROVER_assigns(FORMAL_MP4_FRAME(reader)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_LOOP_read_items_0                                                                   \
    __CPROVER_assigns(cursor, atom, old_genre, has_old_genre, FORMAL_MP4_FRAME(reader))            \
        __CPROVER_loop_invariant(FORMAL_TAGS_GOOD(reader) &&                                       \
                                 (!has_old_genre || FORMAL_MP4_ATOM(&old_genre, reader)) &&        \
                                 (cursor == __CPROVER_loop_entry(cursor) || cursor <= ilst->end))  \
            __CPROVER_decreases((int64_t)ilst->end - (int64_t)cursor)

#define FORMAL_CONTRACT_read_tags                                                                  \
    FORMAL_MP4_READER(reader)                                                                      \
    FORMAL_MP4_PARENT(moov, reader)                                                                \
    __CPROVER_assigns(FORMAL_MP4_FRAME(reader)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_tags_read_mp4                                                              \
    FORMAL_MP4_READER(reader)                                                                      \
    __CPROVER_assigns(FORMAL_MP4_FRAME(reader)) FORMAL_TAG_READER_KEPT(reader)
