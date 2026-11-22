#pragma once

#include "tags.h"

static bool read_file_bytes(void *context, uint32_t offset, void *buffer, uint32_t length);

#define FORMAL_VORBIS_SOURCE(source, reader)                                                       \
    ((source).read == formal_card_read ||                                                          \
     ((source).read == read_file_bytes &&                                                          \
      __CPROVER_pointer_in_range_dfcc((reader), (source).context, (reader))))

#define FORMAL_FLAC_READER(reader)                                                                 \
    __CPROVER_requires(__CPROVER_is_fresh(reader, sizeof(TagReader)))                              \
        __CPROVER_requires(__CPROVER_is_fresh((reader)->tags, sizeof(TrackTags)))                  \
            __CPROVER_requires((reader)->source.read == formal_card_read &&                        \
                               (reader)->unsync == NULL && (reader)->source.context == NULL)       \
                __CPROVER_requires(FORMAL_TAGS_GOOD(reader))

#define FORMAL_CONTRACT_offer_entry                                                                \
    FORMAL_FLAC_READER(reader)                                                                     \
    __CPROVER_requires(FORMAL_VORBIS_SOURCE(source, reader))                                       \
        __CPROVER_assigns(reader->present, reader->replay_gain,                                    \
                          __CPROVER_object_whole(reader->tags)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_tags_read_vorbis_comment                                                   \
    FORMAL_FLAC_READER(reader)                                                                     \
    __CPROVER_requires(FORMAL_VORBIS_SOURCE(source, reader))                                       \
        __CPROVER_assigns(reader->present, reader->replay_gain,                                    \
                          __CPROVER_object_whole(reader->tags)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_LOOP_tags_read_vorbis_comment_0                                                     \
    __CPROVER_assigns(entry, cursor, __CPROVER_object_whole(word), reader->present,                \
                      reader->replay_gain, __CPROVER_object_whole(reader->tags))                   \
        __CPROVER_loop_invariant(entry <= count && FORMAL_TAGS_GOOD(reader))                       \
            __CPROVER_decreases(count - entry)

#define FORMAL_CONTRACT_read_flac_picture                                                          \
    FORMAL_FLAC_READER(reader)                                                                     \
    __CPROVER_requires(offset <= end && end <= reader->size && end >= 4)                           \
        __CPROVER_assigns(__CPROVER_object_whole(reader->tags)) FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_CONTRACT_tags_read_flac                                                             \
    FORMAL_FLAC_READER(reader)                                                                     \
    __CPROVER_assigns(reader->present, reader->replay_gain, __CPROVER_object_whole(reader->tags))  \
        FORMAL_TAG_READER_KEPT(reader)

#define FORMAL_LOOP_tags_read_flac_0                                                               \
    __CPROVER_assigns(last, offset, found_info, __CPROVER_object_whole(header), reader->present,   \
                      reader->replay_gain, __CPROVER_object_whole(reader->tags))                   \
        __CPROVER_loop_invariant(                                                                  \
            FORMAL_TAGS_GOOD(reader) &&                                                            \
            (offset == __CPROVER_loop_entry(offset) || offset <= reader->size))                    \
            __CPROVER_decreases((int64_t)reader->size - (int64_t)offset)
