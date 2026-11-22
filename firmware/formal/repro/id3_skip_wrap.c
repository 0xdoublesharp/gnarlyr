#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "library/track_tags.h"

enum {
    FILE_SIZE = 0xFFFFFFF0u,
    STEP = 0x0FFFFFFFu,
    HEADER_BYTES = 10,
    MAX_HEADERS = 32,
    READ_LIMIT = 4096
};

typedef struct SparseFile {
    uint32_t offsets[MAX_HEADERS];
    uint32_t sizes[MAX_HEADERS];
    uint32_t count;
    uint32_t reads;
    uint32_t reads_at_zero;
} SparseFile;

static void put_synchsafe(uint8_t *bytes, uint32_t value)
{
    for (int index = 3; index >= 0; index--) {
        bytes[index] = (uint8_t)(value & 0x7F);
        value >>= 7;
    }
}

static void build_chain(SparseFile *file)
{
    uint64_t offset = 0;
    file->count = 0;
    while (file->count < MAX_HEADERS) {
        uint64_t left = ((uint64_t)1 << 32) - offset - HEADER_BYTES;
        uint32_t size = left <= STEP ? (uint32_t)left : STEP;
        file->offsets[file->count] = (uint32_t)offset;
        file->sizes[file->count] = size;
        file->count++;
        offset += HEADER_BYTES + (uint64_t)size;
        if (offset == ((uint64_t)1 << 32)) {
            return;
        }
    }
}

static bool sparse_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    SparseFile *file = context;
    if (++file->reads > READ_LIMIT || offset > FILE_SIZE || length > FILE_SIZE - offset) {
        return false;
    }
    file->reads_at_zero += offset == 0;
    memset(buffer, 0, length);
    for (uint32_t index = 0; index < file->count; index++) {
        if (file->offsets[index] == offset && length >= HEADER_BYTES) {
            uint8_t *bytes = buffer;
            memcpy(bytes, "ID3\x04\x00\x00", 6);
            put_synchsafe(bytes + 6, file->sizes[index]);
        }
    }
    return true;
}

static bool cycles(TagFormat format, const char *name)
{
    SparseFile file;
    TrackTags tags;
    build_chain(&file);
    file.reads = 0;
    file.reads_at_zero = 0;
    ByteSource source = {.read = sparse_read, .context = &file};
    (void)track_tags_read(source, FILE_SIZE, format, &tags);
    bool cycled = file.reads > READ_LIMIT && file.reads_at_zero > 2;
    printf("%s: %u ID3 headers, %u reads, %u at offset 0: %s\n", name, file.count, file.reads,
           file.reads_at_zero,
           cycled ? "the ID3 skip loop wraps to offset 0 and cycles" : "no cycle");
    return cycled;
}

int main(void)
{
    bool flac = cycles(TAG_FORMAT_FLAC, "FLAC");
    bool mp3 = cycles(TAG_FORMAT_MP3, "MP3");
    return flac && mp3 ? 0 : 1;
}
