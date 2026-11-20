#include "library/track_tags.h"
#include "formal.h"

static void check_tags(const TrackTags *tags)
{
    for (int field = 0; field < TAG_FIELD_COUNT; field++) {
        __CPROVER_assert(tags->text[field][TRACK_TAG_TEXT_CAPACITY - 1] == '\0',
                         "every tag text ends inside its field");
    }
    __CPROVER_assert(tags->picture.kind <= TAG_PICTURE_UNSYNC, "the picture kind is known");
    if (tags->picture.kind == TAG_PICTURE_RAW) {
        __CPROVER_assert(tags->picture.offset <= formal_card_size &&
                             tags->picture.length <= formal_card_size - tags->picture.offset,
                         "a raw picture lies inside the file");
    }
}

void harness(void)
{
    TrackTags tags;
    formal_card_fill();
    (void)track_tags_read(formal_card_source(), formal_card_size, FORMAL_TAG_FORMAT, &tags);
    check_tags(&tags);
}
