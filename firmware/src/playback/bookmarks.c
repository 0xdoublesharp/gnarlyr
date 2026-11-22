#include "playback/bookmarks.h"

#include "bookmarks_format.h"
#include "core/bytes.h"
#include "core/record.h"

static int find_index(const Bookmarks *bookmarks, uint32_t key)
{
    for (int index = 0; index < bookmarks->count; index++) {
        if (bookmarks->entries[index].key == key) {
            return index;
        }
    }
    return -1;
}

void bookmarks_clear(Bookmarks *bookmarks)
{
    bookmarks->count = 0;
}

bool bookmarks_find(const Bookmarks *bookmarks, uint32_t key, Bookmark *bookmark)
{
    int index = find_index(bookmarks, key);
    if (index < 0) {
        return false;
    }
    *bookmark = bookmarks->entries[index];
    return true;
}

static void shift_down(Bookmarks *bookmarks, int last)
{
    for (int index = last; index > 0; index--) {
        bookmarks->entries[index] = bookmarks->entries[index - 1];
    }
}

void bookmarks_put(Bookmarks *bookmarks, Bookmark bookmark)
{
    int index = find_index(bookmarks, bookmark.key);
    if (index < 0) {
        if (bookmarks->count < BOOKMARKS_CAPACITY) {
            bookmarks->count++;
        }
        index = bookmarks->count - 1;
    }
    shift_down(bookmarks, index);
    bookmarks->entries[0] = bookmark;
}

bool bookmark_finished(Bookmark bookmark)
{
    return (bookmark.flags & BOOKMARK_FINISHED) != 0;
}

static void encode_record(Bookmark bookmark, uint8_t *record)
{
    bytes_put_le32(record + KEY_OFFSET, bookmark.key);
    bytes_put_le32(record + TRACK_OFFSET, bookmark.track_key);
    bytes_put_le32(record + POSITION_OFFSET, bookmark.position_ms);
    bytes_put_le16(record + CHAPTER_OFFSET, bookmark.chapter);
    record[FLAGS_OFFSET] = bookmark.flags;
    record[FLAGS_OFFSET + 1] = 0;
}

uint32_t bookmarks_encode(const Bookmarks *bookmarks, uint8_t *bytes)
{
    record_begin(bytes, &KIND);
    bytes_put_le16(bytes + COUNT_FIELD, bookmarks->count);
    bytes_put_le32(bytes + COUNT_FIELD + 2, 0);
    for (uint16_t index = 0; index < bookmarks->count; index++) {
        encode_record(bookmarks->entries[index],
                      bytes + RECORDS_FIELD + (uint32_t)index * BOOKMARK_RECORD_SIZE);
    }
    uint32_t checksum_field = RECORDS_FIELD + (uint32_t)bookmarks->count * BOOKMARK_RECORD_SIZE;
    record_seal(bytes, checksum_field);
    return checksum_field + CHECKSUM_SIZE;
}
