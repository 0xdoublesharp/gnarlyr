#pragma once

#include "tags.h"

#define FORMAL_ID3_FRAME(frame, reader)                                                            \
    __CPROVER_requires(__CPROVER_is_fresh(frame, sizeof(Frame))) __CPROVER_requires(               \
        (frame)->offset <= (reader)->size && (frame)->length <= (reader)->size - (frame)->offset)  \
        __CPROVER_requires(!(frame)->unsync || (reader)->source.read == formal_card_read)

#define FORMAL_ID3_TAG(tag)                                                                        \
    __CPROVER_requires(__CPROVER_is_fresh(tag, sizeof(Id3Tag)))                                    \
        __CPROVER_requires((tag)->version >= 2 && (tag)->version <= 4)

#define FORMAL_CONTRACT_remove_unsync                                                              \
    __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(data, length))          \
        __CPROVER_assigns(__CPROVER_object_upto(data, length)) __CPROVER_ensures(                  \
            __CPROVER_return_value <= length && 2 * (uint64_t)__CPROVER_return_value >= length)

#define FORMAL_LOOP_remove_unsync_0                                                                \
    __CPROVER_assigns(kept, index, __CPROVER_object_upto(data, length))                            \
        __CPROVER_loop_invariant(kept <= index && index <= length && 2 * (uint64_t)kept >= index)  \
            __CPROVER_decreases(length - index)

#define FORMAL_CONTRACT_value_end                                                                  \
    __CPROVER_requires(length <= FRAME_CAPACITY && __CPROVER_is_fresh(data, length))               \
        __CPROVER_requires((width == 1 || width == 2) && start <= length)                          \
            __CPROVER_ensures(start <= __CPROVER_return_value && __CPROVER_return_value <= length)

#define FORMAL_LOOP_value_end_0                                                                    \
    __CPROVER_assigns(index) __CPROVER_loop_invariant(start <= index && index <= length)           \
        __CPROVER_decreases(length - index)

#define FORMAL_LOOP_for_each_value_0                                                               \
    __CPROVER_assigns(start, __CPROVER_object_whole(value), tag->year;                             \
                      FORMAL_TAG_READER_FRAME(reader))                                             \
        __CPROVER_loop_invariant(1 <= start && start <= length + width &&                          \
                                 FORMAL_TAGS_GOOD(reader))                                         \
            __CPROVER_decreases((int64_t)length - (int64_t)start)

#define FORMAL_CONTRACT_read_text_frame                                                            \
    FORMAL_TAG_READER(reader)                                                                      \
    __CPROVER_requires(__CPROVER_is_fresh(frame, sizeof(Frame)))                                   \
        __CPROVER_requires(__CPROVER_is_fresh(name, sizeof(FrameName))) __CPROVER_requires(        \
            name->field >= 0 && name->field < TAG_FIELD_COUNT &&                                   \
            (name->kind == FRAME_TEXT || name->kind == FRAME_GENRE || name->kind == FRAME_YEAR))   \
            __CPROVER_requires(__CPROVER_is_fresh(tag, sizeof(Id3Tag)))                            \
                __CPROVER_assigns(tag->year, FORMAL_TAG_READER_FRAME(reader))                      \
                    FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_offer_genre_text                                                           \
    FORMAL_TAG_READER(reader)                                                                      \
    __CPROVER_requires(length < TRACK_TAG_TEXT_CAPACITY && __CPROVER_is_fresh(text, length + 1))   \
        __CPROVER_requires(text[length] == '\0')                                                   \
            __CPROVER_assigns(FORMAL_TAG_READER_FRAME(reader)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_skip_value                                                                 \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(FORMAL_SOURCE_ANY(reader)) __CPROVER_requires(width == 1 || width == 2) \
            __CPROVER_assigns(__CPROVER_object_upto(reader->source.context, sizeof(TagUnsync)))    \
                __CPROVER_ensures(__CPROVER_return_value <= end)

#define FORMAL_LOOP_skip_value_0                                                                   \
    __CPROVER_assigns(offset, __CPROVER_object_whole(unit),                                        \
                      __CPROVER_object_upto(reader->source.context, sizeof(TagUnsync)))            \
        __CPROVER_loop_invariant(offset == __CPROVER_loop_entry(offset) || offset <= end)          \
            __CPROVER_decreases((int64_t)end - (int64_t)offset)

#define FORMAL_CONTRACT_read_picture_frame                                                         \
    FORMAL_TAG_READER(reader)                                                                      \
    FORMAL_ID3_FRAME(frame, reader)                                                                \
    __CPROVER_assigns(reader->source, reader->unsync, FORMAL_TAG_READER_FRAME(reader))             \
        FORMAL_SOURCE_KEPT(reader) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_read_replay_gain_frame                                                     \
    FORMAL_TAG_READER(reader)                                                                      \
    __CPROVER_requires(__CPROVER_is_fresh(frame, sizeof(Frame)))                                   \
        __CPROVER_assigns(FORMAL_TAG_READER_FRAME(reader)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_read_frame                                                                 \
    FORMAL_TAG_READER(reader)                                                                      \
    FORMAL_ID3_FRAME(frame, reader)                                                                \
    FORMAL_ID3_TAG(tag)                                                                            \
    __CPROVER_assigns(reader->source, reader->unsync, tag->year, FORMAL_TAG_READER_FRAME(reader))  \
        FORMAL_SOURCE_KEPT(reader) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_read_frames                                                                \
    FORMAL_TAG_READER(reader)                                                                      \
    FORMAL_ID3_TAG(tag)                                                                            \
    __CPROVER_requires(tag->end <= reader->size)                                                   \
        __CPROVER_requires(tag->version != 4 || reader->source.read == formal_card_read)           \
            __CPROVER_requires(!tag->unsync || tag->version == 4) __CPROVER_assigns(               \
                reader->source, reader->unsync, tag->year, FORMAL_TAG_READER_FRAME(reader))        \
                FORMAL_SOURCE_KEPT(reader) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_LOOP_read_frames_0                                                                  \
    __CPROVER_assigns(cursor, __CPROVER_object_whole(header), reader->source, reader->unsync,      \
                      tag->year;                                                                   \
                      FORMAL_TAG_READER_FRAME(reader))                                             \
        __CPROVER_loop_invariant(reader->source.context ==                                         \
                                     __CPROVER_loop_entry(reader->source.context) &&               \
                                 reader->unsync == __CPROVER_loop_entry(reader->unsync) &&         \
                                 FORMAL_SOURCE_BY_CONTEXT(reader) && FORMAL_TAGS_GOOD(reader) &&   \
                                 (cursor == __CPROVER_loop_entry(cursor) || cursor <= tag->end))   \
            __CPROVER_decreases((int64_t)tag->end - (int64_t)cursor)

#define FORMAL_LOOP_read_frames_1                                                                  \
    __CPROVER_assigns(index, frame.id) __CPROVER_loop_invariant(index <= 4)                        \
        __CPROVER_decreases(4 - index)

#define FORMAL_CONTRACT_tags_read_id3v2                                                            \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader))) __CPROVER_requires(          \
        __CPROVER_is_fresh(reader->tags, sizeof(TrackTags)))                                       \
        __CPROVER_requires(reader->source.read == formal_card_read && reader->unsync == NULL)      \
            __CPROVER_requires(reader->source.context == NULL && FORMAL_TAGS_GOOD(reader))         \
                __CPROVER_requires(__CPROVER_is_fresh(tag_size, sizeof(uint32_t)))                 \
                    __CPROVER_assigns(*tag_size, reader->source, reader->unsync, reader->present,  \
                                      reader->replay_gain, __CPROVER_object_whole(reader->tags))   \
                        __CPROVER_ensures(reader->source.read == formal_card_read &&               \
                                          reader->unsync == NULL) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_tags_read_id3v1                                                            \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(__CPROVER_is_fresh(reader->tags, sizeof(TrackTags)))                    \
            __CPROVER_requires(reader->source.read == formal_card_read &&                          \
                               reader->source.context == NULL)                                     \
                __CPROVER_requires(reader->unsync == NULL && FORMAL_TAGS_GOOD(reader))             \
                    __CPROVER_assigns(reader->present, __CPROVER_object_whole(reader->tags))       \
                        FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_tags_skip_id3v2                                                            \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(reader->source.read == formal_card_read &&                              \
                           reader->source.context == NULL && reader->unsync == NULL)               \
            __CPROVER_ensures(__CPROVER_return_value <= reader->size)

#define FORMAL_LOOP_tags_skip_id3v2_0                                                              \
    __CPROVER_assigns(offset, __CPROVER_object_whole(header))                                      \
        __CPROVER_loop_invariant(offset <= reader->size)                                           \
            __CPROVER_decreases((int64_t)reader->size - (int64_t)offset)
