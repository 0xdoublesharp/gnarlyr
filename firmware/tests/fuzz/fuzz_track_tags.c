#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "library/track_tags.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_tags(const TrackTags *tags, uint32_t size)
{
    for (int field = 0; field < TAG_FIELD_COUNT; field++) {
        fuzz_require(memchr(tags->text[field], '\0', TRACK_TAG_TEXT_CAPACITY) != NULL);
    }
    fuzz_require(tags->picture.kind >= TAG_PICTURE_NONE &&
                 tags->picture.kind <= TAG_PICTURE_BASE64_FLAC_BLOCK);
    if (tags->picture.kind == TAG_PICTURE_RAW) {
        fuzz_require(tags->picture.offset <= size);
        fuzz_require(tags->picture.length <= size - tags->picture.offset);
    }
}

static bool same_tags(const TrackTags *first, const TrackTags *second)
{
    bool same = first->duration_ms == second->duration_ms &&
                first->picture.kind == second->picture.kind &&
                first->picture.front == second->picture.front &&
                first->picture.offset == second->picture.offset &&
                first->picture.length == second->picture.length;
    for (int field = 0; field < TAG_FIELD_COUNT && same; field++) {
        same = strcmp(first->text[field], second->text[field]) == 0;
    }
    return same;
}

static void check_format(const uint8_t *data, uint32_t size, TagFormat format)
{
    FuzzSource first_source = {.bytes = data, .size = size};
    FuzzSource second_source = first_source;
    TrackTags first;
    TrackTags second;
    bool first_read = track_tags_read(fuzz_source(&first_source), size, format, &first);
    bool second_read = track_tags_read(fuzz_source(&second_source), size, format, &second);
    check_tags(&first, size);
    check_tags(&second, size);
    fuzz_require(first_read == second_read);
    fuzz_require(same_tags(&first, &second));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    for (int format = TAG_FORMAT_FLAC; format <= TAG_FORMAT_AIFF; format++) {
        check_format(data, (uint32_t)size, (TagFormat)format);
    }
    return 0;
}
