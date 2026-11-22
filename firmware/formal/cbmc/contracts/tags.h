#pragma once

#include "formal.h"

#define FORMAL_TAG_TEXTS_END(tags)                                                                 \
    ((tags)->text[0][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[1][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[2][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[3][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[4][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[5][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[6][TRACK_TAG_TEXT_CAPACITY - 1] == '\0' &&                                       \
     (tags)->text[7][TRACK_TAG_TEXT_CAPACITY - 1] == '\0')

#define FORMAL_PICTURE_INSIDE(tags, size)                                                          \
    ((tags)->picture.kind <= TAG_PICTURE_UNSYNC &&                                                 \
     ((tags)->picture.kind != TAG_PICTURE_RAW ||                                                   \
      ((tags)->picture.offset <= (size) &&                                                         \
       (tags)->picture.length <= (size) - (tags)->picture.offset)))

#define FORMAL_TAGS_GOOD(reader)                                                                   \
    (FORMAL_TAG_TEXTS_END((reader)->tags) && FORMAL_PICTURE_INSIDE((reader)->tags, (reader)->size))

#define FORMAL_SOURCE_ANY(reader)                                                                  \
    (((reader)->source.read == formal_card_read && (reader)->source.context == NULL &&             \
      (reader)->unsync == NULL) ||                                                                 \
     ((reader)->source.read == tag_unsync_read &&                                                  \
      __CPROVER_is_fresh((reader)->unsync, sizeof(TagUnsync)) &&                                   \
      (reader)->source.context == (reader)->unsync))

#define FORMAL_UNSYNC_LINKED(reader)                                                               \
    (((reader)->source.read == formal_card_read && (reader)->unsync == NULL) ||                    \
     ((reader)->source.read == tag_unsync_read && (reader)->unsync == (reader)->source.context))

#define FORMAL_TAG_READER(reader)                                                                  \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(__CPROVER_is_fresh((reader)->tags, sizeof(TrackTags)))                  \
            __CPROVER_requires(FORMAL_SOURCE_ANY(reader) && FORMAL_UNSYNC_LINKED(reader))          \
                __CPROVER_requires(FORMAL_TAGS_GOOD(reader))

#define FORMAL_SOURCE_BY_CONTEXT(reader)                                                           \
    ((reader)->source.context == NULL ? (reader)->source.read == formal_card_read                  \
                                      : (reader)->source.read == tag_unsync_read)

#define FORMAL_SOURCE_KEPT(reader)                                                                 \
    __CPROVER_ensures((reader)->source.context == __CPROVER_old((reader)->source.context) &&       \
                      (reader)->unsync == __CPROVER_old((reader)->unsync) &&                       \
                      FORMAL_SOURCE_BY_CONTEXT(reader))

#define FORMAL_TAG_READER_FRAME(reader)                                                            \
    (reader)->present, (reader)->replay_gain, __CPROVER_object_whole((reader)->tags),              \
        __CPROVER_object_upto((reader)->source.context, sizeof(TagUnsync))

#define FORMAL_TAG_READER_KEPT(reader)                                                             \
    __CPROVER_ensures(FORMAL_TAG_TEXTS_END((reader)->tags))                                        \
        __CPROVER_ensures(FORMAL_PICTURE_INSIDE((reader)->tags, (reader)->size))

#define FORMAL_CONTRACT_tag_read                                                                   \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(FORMAL_SOURCE_ANY(reader)) __CPROVER_requires(                          \
            length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(buffer, length))                   \
            __CPROVER_assigns(__CPROVER_object_upto(buffer, length),                               \
                              __CPROVER_object_upto(reader->source.context, sizeof(TagUnsync)))    \
                __CPROVER_ensures(!__CPROVER_return_value ||                                       \
                                  (offset <= reader->size && length <= reader->size - offset))

#define FORMAL_CONTRACT_tag_decode                                                                 \
    __CPROVER_requires(__CPROVER_is_fresh(out, TRACK_TAG_TEXT_CAPACITY))                           \
        __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(text, length))      \
            __CPROVER_requires(encoding <= TAG_UTF16BE)                                            \
                __CPROVER_assigns(__CPROVER_object_upto(out, TRACK_TAG_TEXT_CAPACITY))             \
                    __CPROVER_ensures(FORMAL_TEXT_ENDS(out, TRACK_TAG_TEXT_CAPACITY))              \
                        __CPROVER_ensures(out[TRACK_TAG_TEXT_CAPACITY - 1] == '\0' ||              \
                                          out[TRACK_TAG_TEXT_CAPACITY - 1] ==                      \
                                              __CPROVER_old(out[TRACK_TAG_TEXT_CAPACITY - 1]))

#define FORMAL_LOOP_tag_decode_0                                                                   \
    __CPROVER_assigns(index, builder, __CPROVER_object_upto(out, TRACK_TAG_TEXT_CAPACITY - 1))     \
        __CPROVER_loop_invariant(index <= length && builder.text == out &&                         \
                                 builder.length < TRACK_TAG_TEXT_CAPACITY &&                       \
                                 builder.content_end <= builder.length &&                          \
                                 out[TRACK_TAG_TEXT_CAPACITY - 1] ==                               \
                                     __CPROVER_loop_entry(out[TRACK_TAG_TEXT_CAPACITY - 1]))       \
            __CPROVER_decreases(length - index)

#define FORMAL_CONTRACT_tag_decode_gain_text                                                       \
    __CPROVER_requires(__CPROVER_is_fresh(out, REPLAY_GAIN_TEXT_CAPACITY))                         \
        __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(text, length))      \
            __CPROVER_requires(encoding <= TAG_UTF16BE)                                            \
                __CPROVER_assigns(__CPROVER_object_upto(out, REPLAY_GAIN_TEXT_CAPACITY))           \
                    __CPROVER_ensures(!__CPROVER_return_value ||                                   \
                                      FORMAL_TEXT_ENDS(out, REPLAY_GAIN_TEXT_CAPACITY))

#define FORMAL_LOOP_tag_decode_gain_text_0                                                         \
    __CPROVER_assigns(at, count, encoding, __CPROVER_object_upto(out, REPLAY_GAIN_TEXT_CAPACITY))  \
        __CPROVER_loop_invariant(at <= length && count < REPLAY_GAIN_TEXT_CAPACITY &&              \
                                 (width == 1 || (at % 2 == length % 2)))                           \
            __CPROVER_decreases(length - at)

#define FORMAL_LOOP_empty_gain_tail_0                                                              \
    __CPROVER_assigns(at) __CPROVER_loop_invariant(at <= length || at == __CPROVER_loop_entry(at)) \
        __CPROVER_decreases(length - at)

#define FORMAL_CONTRACT_replay_gain_field                                                          \
    __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT)                                              \
        __CPROVER_requires(key == NULL || __CPROVER_is_fresh(key, length))                         \
            __CPROVER_ensures(__CPROVER_return_value <= REPLAY_GAIN_NO_FIELD)

#define FORMAL_CONTRACT_replay_gain_offer                                                          \
    __CPROVER_requires(__CPROVER_is_fresh(tags, sizeof(ReplayGainTags)))                           \
        __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(text, length))      \
            __CPROVER_assigns(*tags)

#define FORMAL_CONTRACT_replay_gain_offer_r128                                                     \
    __CPROVER_requires(__CPROVER_is_fresh(tags, sizeof(ReplayGainTags)))                           \
        __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(text, length))      \
            __CPROVER_assigns(*tags)

#define FORMAL_CONTRACT_tag_unsync_read                                                            \
    __CPROVER_requires(__CPROVER_is_fresh(context, sizeof(TagUnsync)))                             \
        __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(buffer, length))    \
            __CPROVER_assigns(__CPROVER_object_upto(buffer, length),                               \
                              __CPROVER_object_upto(context, sizeof(TagUnsync)))

#define FORMAL_LOOP_tag_unsync_read_0                                                              \
    __CPROVER_assigns(length, out, target, __CPROVER_object_whole(chunk), view->raw,               \
                      view->decoded, view->after_ff,                                               \
                      __CPROVER_object_upto(buffer, __CPROVER_loop_entry(length)))                 \
        __CPROVER_loop_invariant(length <= __CPROVER_loop_entry(length) &&                         \
                                 out ==                                                            \
                                     (uint8_t *)buffer + (__CPROVER_loop_entry(length) - length))  \
            __CPROVER_decreases(view->end - view->raw)

#define FORMAL_LOOP_tag_unsync_read_1                                                              \
    __CPROVER_assigns(length, out, __CPROVER_object_upto(buffer, __CPROVER_loop_entry(length)))    \
        __CPROVER_loop_invariant(out + length ==                                                   \
                                 __CPROVER_loop_entry(out) + __CPROVER_loop_entry(length))         \
            __CPROVER_decreases(length)

#define FORMAL_CONTRACT_tag_set_duration                                                           \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(__CPROVER_is_fresh(reader->tags, sizeof(TrackTags)))                    \
            __CPROVER_assigns(reader->tags->duration_ms)
