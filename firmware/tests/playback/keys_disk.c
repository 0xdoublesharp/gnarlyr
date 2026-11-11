#include "keys_disk.h"

#include <string.h>

#include "core/hash.h"

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    KeysDisk *disk = context;
    disk->reads++;
    if ((disk->refuse_reads_after != 0 && disk->reads > disk->refuse_reads_after) ||
        offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        return false;
    }
    memcpy(bytes, disk->bytes + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    KeysDisk *disk = context;
    if (offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        return false;
    }
    disk->writes++;
    if (disk->writes == disk->cut_write) {
        return false;
    }
    memcpy(disk->bytes + offset, bytes, length);
    return true;
}

void keys_disk_reset(KeysDisk *disk)
{
    memset(disk, 0, sizeof *disk);
}

StoreFile keys_disk_file(KeysDisk *disk)
{
    return (StoreFile){
        .source = {read_disk, disk}, .write = write_disk, .size = sizeof disk->bytes};
}

void keys_disk_publish(KeysDisk *disk, const TrackKey *entries, uint32_t count, uint16_t latest)
{
    static const StoreKind kind = {.magic = {'D', 'S', 'T', 'K'}, .version = 1};
    uint8_t *payload = disk->bytes + STORE_BLOCK_BYTES;
    for (uint32_t index = 0; index < count; index++) {
        uint8_t *entry = payload + index * TRACK_KEYS_ENTRY_BYTES;
        store_put32(entry, entries[index].key);
        store_put16(entry + 4, entries[index].track);
        store_put16(entry + 6, entries[index].arrival);
    }
    uint32_t bytes = count * TRACK_KEYS_ENTRY_BYTES;
    StoreHeader header = {.generation = 1,
                          .bytes = bytes,
                          .hash = hash_update(HASH_START, payload, bytes),
                          .binding = 0,
                          .count = latest};
    memset(disk->bytes, 0, STORE_BLOCK_BYTES);
    store_encode(&kind, &header, disk->bytes);
}
