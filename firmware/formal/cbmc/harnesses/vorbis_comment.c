#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    TrackTags tags = {.duration_ms = 0, .picture = {.kind = TAG_PICTURE_NONE}};
    formal_card_fill();
    TagReader reader = {.source = formal_card_source(),
                        .size = formal_card_size,
                        .tags = &tags,
                        .opus = nondet_bool()};
    uint32_t offset = formal_below(formal_card_size + 1);
    uint32_t limit = offset + formal_below(formal_card_size - offset + 1);
    (void)tags_read_vorbis_comment(&reader, reader.source, offset, limit);
    for (int field = 0; field < TAG_FIELD_COUNT; field++) {
        __CPROVER_assert(tags.text[field][TRACK_TAG_TEXT_CAPACITY - 1] == '\0',
                         "every tag text ends inside its field");
    }
    if (tags.picture.kind != TAG_PICTURE_NONE) {
        __CPROVER_assert(tags.picture.offset <= formal_card_size,
                         "a picture starts inside the file");
    }
}
