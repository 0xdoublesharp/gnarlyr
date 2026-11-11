#include "library/covers.h"

enum {
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    FULL_FIELD = 8,
    HALF_FIELD = 10,
    BYTES_PER_PIXEL = 2,
    FULL_BYTES = COVER_FULL_PIXELS * BYTES_PER_PIXEL,
    HALF_BYTES = COVER_HALF_PIXELS * BYTES_PER_PIXEL,
    RECORD_BYTES = FULL_BYTES + HALF_BYTES,
    COVERS_WINDOW = 64,
    COVERS_GUESSES = 2,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'D', 'S', 'C', 'V'};

static uint16_t u16_at(const uint8_t *bytes, uint32_t offset)
{
    return (uint16_t)(bytes[offset] | bytes[offset + 1] << BITS_PER_BYTE);
}

static uint32_t u32_at(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t)u16_at(bytes, offset) | (uint32_t)u16_at(bytes, offset + 2) << 16;
}

static bool header_valid(const uint8_t *header)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (header[index] != MAGIC[index]) {
            return false;
        }
    }
    return u16_at(header, VERSION_FIELD) == COVERS_VERSION &&
           u16_at(header, FULL_FIELD) == COVER_FULL_SIZE &&
           u16_at(header, HALF_FIELD) == COVER_HALF_SIZE;
}

bool covers_open(Covers *covers, ByteSource source, uint32_t size)
{
    uint8_t header[COVERS_HEADER_SIZE];
    *covers = (Covers){.source = source, .size = size, .count = 0};
    if (size < COVERS_HEADER_SIZE || !source.read(source.context, 0, header, sizeof header) ||
        !header_valid(header)) {
        return false;
    }
    uint32_t count = u16_at(header, COUNT_FIELD);
    uint64_t needed = COVERS_HEADER_SIZE + (uint64_t)count * (COVERS_ENTRY_SIZE + RECORD_BYTES);
    if (needed > size) {
        return false;
    }
    covers->count = count;
    return true;
}

typedef struct CoverRange {
    uint32_t low;
    uint32_t high;
    uint64_t low_key;
    uint64_t high_key;
    uint32_t windows;
} CoverRange;

static uint32_t window_start(const CoverRange *range, uint32_t key, bool interpolate)
{
    uint64_t span = range->high_key - range->low_key;
    uint64_t guess = ((uint64_t)key - range->low_key) * (range->high - range->low) / span;
    uint32_t middle = interpolate ? range->low + (uint32_t)guess : (range->low + range->high) / 2;
    uint32_t start =
        middle > range->low + COVERS_WINDOW / 2 ? middle - COVERS_WINDOW / 2 : range->low;
    return range->high - start < COVERS_WINDOW && range->high > COVERS_WINDOW + range->low
               ? range->high - COVERS_WINDOW
               : start;
}

static int found_record(const Covers *covers, const uint8_t *entry, uint32_t key, uint32_t *record)
{
    *record = u32_at(entry, 4);
    return u32_at(entry, 0) == key && *record <= covers->size &&
           covers->size - *record >= RECORD_BYTES;
}

static int search_window(const Covers *covers, uint32_t key, CoverRange *range, uint32_t *record)
{
    uint8_t entries[COVERS_WINDOW * COVERS_ENTRY_SIZE];
    uint32_t first = window_start(range, key, range->windows++ < COVERS_GUESSES);
    uint32_t count = range->high - first < COVERS_WINDOW ? range->high - first : COVERS_WINDOW;
    uint32_t offset = COVERS_HEADER_SIZE + first * COVERS_ENTRY_SIZE;
    if (!covers->source.read(covers->source.context, offset, entries, count * COVERS_ENTRY_SIZE)) {
        return 0;
    }
    uint32_t index = 0;
    while (index < count && u32_at(entries, index * COVERS_ENTRY_SIZE) < key) {
        index++;
    }
    if (index == count) {
        range->low = first + count;
        range->low_key = (uint64_t)u32_at(entries, (count - 1) * COVERS_ENTRY_SIZE) + 1;
        return -1;
    }
    if (index > 0 || u32_at(entries, 0) == key) {
        return found_record(covers, entries + index * COVERS_ENTRY_SIZE, key, record);
    }
    range->high = first;
    range->high_key = u32_at(entries, 0);
    return -1;
}

bool covers_find(const Covers *covers, uint32_t key, uint32_t *record)
{
    CoverRange range = {0, covers->count, 0, (uint64_t)UINT32_MAX + 1, 0};
    while (range.low < range.high && key >= range.low_key && key < range.high_key) {
        int result = search_window(covers, key, &range, record);
        if (result >= 0) {
            return result == 1;
        }
    }
    return false;
}

static void unpack_pixels(uint16_t *pixels, uint32_t count)
{
    const uint8_t *bytes = (const uint8_t *)pixels;
    for (uint32_t index = 0; index < count; index++) {
        uint16_t value = u16_at(bytes, index * BYTES_PER_PIXEL);
        pixels[index] = value;
    }
}

bool covers_read(const Covers *covers, uint32_t record, CoverSize size, uint16_t *pixels)
{
    bool full = size == COVER_FULL;
    uint32_t offset = full ? record : record + FULL_BYTES;
    uint32_t length = full ? FULL_BYTES : HALF_BYTES;
    if (!covers->source.read(covers->source.context, offset, pixels, length)) {
        return false;
    }
    unpack_pixels(pixels, full ? COVER_FULL_PIXELS : COVER_HALF_PIXELS);
    return true;
}
