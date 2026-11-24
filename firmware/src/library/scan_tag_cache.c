#include "scan_tag_cache.h"

#include "core/hash.h"

enum {
    TAG_CACHE_VERSION = 0x01545344,
    BANK_BYTES = SCAN_TAG_CACHE_BYTES / 2,
    HEADER_SPACE = 512
};

const char SCAN_TAG_CACHE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/tagcache.bin";

_Static_assert(sizeof(TagCacheHeader) == 16, "tag cache header layout");
_Static_assert(sizeof(TagCacheRecord) == 36, "tag cache record layout");

static uint32_t checksum(uint32_t hash, const TagCacheHeader *header)
{
    return hash_update(hash, &header->sequence, 2 * sizeof(uint32_t));
}

static bool verified(ScanTagCache *cache, uint32_t start)
{
    uint32_t left = cache->prior.length;
    uint32_t hash = HASH_START;
    while (left > 0) {
        uint32_t length = left < TAG_CACHE_BUFFER ? left : TAG_CACHE_BUFFER;
        if (!store_read(&cache->store, start, cache->input_bytes, length)) {
            return false;
        }
        hash = hash_update(hash, cache->input_bytes, length);
        start += length;
        left -= length;
    }
    return checksum(hash, &cache->prior) == cache->prior.checksum;
}

void scan_tag_cache_begin(ScanTagCache *cache, const BuilderScratch *io, bool full)
{
    store_init(&cache->store, io, 0);
    cache->prior = (TagCacheHeader){0};
    bool valid = store_read(&cache->store, 0, &cache->prior, sizeof cache->prior) &&
                 cache->prior.version == TAG_CACHE_VERSION &&
                 cache->prior.length <= BANK_BYTES - HEADER_SPACE;
    uint32_t bank = (cache->prior.sequence & 1u) * BANK_BYTES;
    cache->sequence = cache->prior.sequence + 1;
    cache->checksum = HASH_START;
    cache->loaded = false;
    uint32_t start = bank + HEADER_SPACE;
    uint32_t end = valid && !full && verified(cache, start) ? start + cache->prior.length : start;
    in_open(&cache->input, &cache->store, start, end, cache->input_bytes, TAG_CACHE_BUFFER);
    out_open(&cache->output, &cache->store,
             (Region){(bank ^ BANK_BYTES) + HEADER_SPACE, BANK_BYTES - HEADER_SPACE},
             cache->output_bytes, TAG_CACHE_BUFFER);
}

static bool advance(ScanTagCache *cache)
{
    TagCacheRecord *record = &cache->record;
    return in_get(&cache->input, record, sizeof *record) && record->path_length > 0 &&
           record->path_length <= SCAN_PATH_CAPACITY &&
           record->text_length <= TAG_FIELD_COUNT * TRACK_TAG_TEXT_CAPACITY &&
           in_get(&cache->input, cache->contents, record->path_length + record->text_length) &&
           cache->contents[record->path_length - 1] == 0;
}

static int walk_order(const uint8_t *left, const char *right)
{
    while (*left != 0 && *left == (uint8_t)*right) {
        left++;
        right++;
    }
    int first = *left == '/' ? 0 : *left;
    int second = *right == '/' ? 0 : (uint8_t)*right;
    return first == second ? (int)*left - (uint8_t)*right : first - second;
}

static bool unpack_tags(const ScanTagCache *cache, TrackTags *tags)
{
    const TagCacheRecord *record = &cache->record;
    const uint8_t *text = cache->contents + record->path_length;
    const uint8_t *end = text + record->text_length;
    for (uint32_t field = 0; field < TAG_FIELD_COUNT; field++) {
        if (text == end || (uint32_t)(end - text) <= *text) {
            return false;
        }
        uint32_t length = *text++;
        for (uint32_t index = 0; index < length; index++) {
            tags->text[field][index] = (char)*text++;
        }
        tags->text[field][length] = '\0';
    }
    tags->duration_ms = record->duration;
    tags->replay_gain = record->gain;
    return text == end;
}

bool scan_tag_cache_find(ScanTagCache *cache, const char *path, const ScanFileStamp *stamp,
                         TrackTags *tags)
{
    for (;;) {
        if (!cache->loaded) {
            cache->loaded = advance(cache);
            if (!cache->loaded) {
                return false;
            }
        }
        int order = walk_order(cache->contents, path);
        if (order > 0) {
            return false;
        }
        cache->loaded = false;
        if (order == 0) {
            const ScanFileStamp *old = &cache->record.stamp;
            return old->size == stamp->size && old->modified == stamp->modified &&
                   old->cluster == stamp->cluster && unpack_tags(cache, tags);
        }
    }
}

static void append(ScanTagCache *cache, const void *bytes, uint32_t length)
{
    if (!cache->store.failed && out_put(&cache->output, bytes, length)) {
        cache->checksum = hash_update(cache->checksum, bytes, length);
    }
}

void scan_tag_cache_add(ScanTagCache *cache, const char *path, const ScanFileStamp *stamp,
                        const TrackTags *tags)
{
    uint32_t path_length = builder_text_length(path) + 1;
    uint8_t *text = cache->packed;
    uint32_t used = 0;
    for (uint32_t field = 0; field < TAG_FIELD_COUNT; field++) {
        uint32_t length = builder_text_length(tags->text[field]);
        text[used++] = (uint8_t)length;
        for (uint32_t index = 0; index < length; index++) {
            text[used++] = (uint8_t)tags->text[field][index];
        }
    }
    TagCacheRecord record = {*stamp, (uint16_t)path_length, (uint16_t)used, tags->duration_ms,
                             tags->replay_gain};
    append(cache, &record, sizeof record);
    append(cache, path, path_length);
    append(cache, text, used);
}

void scan_tag_cache_finish(ScanTagCache *cache)
{
    if (cache->store.failed || cache->store.io_failed || !out_close(&cache->output)) {
        return;
    }
    TagCacheHeader header = {TAG_CACHE_VERSION, cache->sequence, cache->output.written, 0};
    header.checksum = checksum(cache->checksum, &header);
    (void)store_write(&cache->store, 0, &header, sizeof header);
}
