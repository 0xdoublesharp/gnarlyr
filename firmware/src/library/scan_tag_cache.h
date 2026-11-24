#pragma once

#include "builder_internal.h"
#include "library/library_scan.h"
#include "library/track_tags.h"

enum {
    TAG_CACHE_BUFFER = 4096,
    TAG_CACHE_RECORD_BYTES = SCAN_PATH_CAPACITY + TAG_FIELD_COUNT * TRACK_TAG_TEXT_CAPACITY
};

typedef struct ScanFileStamp {
    uint32_t size;
    uint32_t modified;
    uint32_t cluster;
} ScanFileStamp;

typedef struct TagCacheHeader {
    uint32_t version;
    uint32_t sequence;
    uint32_t length;
    uint32_t checksum;
} TagCacheHeader;

typedef struct TagCacheRecord {
    ScanFileStamp stamp;
    uint16_t path_length;
    uint16_t text_length;
    uint32_t duration;
    ReplayGain gain;
} TagCacheRecord;

typedef struct ScanTagCache {
    Store store;
    In input;
    Out output;
    TagCacheHeader prior;
    TagCacheRecord record;
    uint32_t sequence;
    uint32_t checksum;
    bool loaded;
    uint8_t input_bytes[TAG_CACHE_BUFFER];
    uint8_t output_bytes[TAG_CACHE_BUFFER];
    uint8_t contents[TAG_CACHE_RECORD_BYTES];
    uint8_t packed[TAG_FIELD_COUNT * TRACK_TAG_TEXT_CAPACITY];
} ScanTagCache;

void scan_tag_cache_begin(ScanTagCache *cache, const BuilderScratch *io, bool full);
bool scan_tag_cache_find(ScanTagCache *cache, const char *path, const ScanFileStamp *stamp,
                         TrackTags *tags);
void scan_tag_cache_add(ScanTagCache *cache, const char *path, const ScanFileStamp *stamp,
                        const TrackTags *tags);
void scan_tag_cache_finish(ScanTagCache *cache);
