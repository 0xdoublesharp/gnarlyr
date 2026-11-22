#include "playback/bookmarks.h"

#include "bookmarks_format.h"
#include "core/bytes.h"
#include "core/record.h"

static Bookmark decode_record(const uint8_t *record)
{
    return (Bookmark){.key = bytes_le32(record + KEY_OFFSET),
                      .track_key = bytes_le32(record + TRACK_OFFSET),
                      .position_ms = bytes_le32(record + POSITION_OFFSET),
                      .chapter = (uint16_t)bytes_le16(record + CHAPTER_OFFSET),
                      .flags = record[FLAGS_OFFSET]};
}

static bool header_valid(const uint8_t *bytes, uint32_t length)
{
    if (length < RECORDS_FIELD + CHECKSUM_SIZE) {
        return false;
    }
    uint32_t count = bytes_le16(bytes + COUNT_FIELD);
    return record_is(bytes, &KIND) && count <= BOOKMARKS_CAPACITY &&
           length >= RECORDS_FIELD + count * BOOKMARK_RECORD_SIZE + CHECKSUM_SIZE;
}

bool bookmarks_decode(const uint8_t *bytes, uint32_t length, Bookmarks *bookmarks)
{
    bookmarks->count = 0;
    if (!header_valid(bytes, length)) {
        return false;
    }
    uint16_t count = (uint16_t)bytes_le16(bytes + COUNT_FIELD);
    uint32_t checksum_field = RECORDS_FIELD + (uint32_t)count * BOOKMARK_RECORD_SIZE;
    if (!record_sealed(bytes, checksum_field)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        bookmarks->entries[index] =
            decode_record(bytes + RECORDS_FIELD + (uint32_t)index * BOOKMARK_RECORD_SIZE);
    }
    bookmarks->count = count;
    return true;
}
