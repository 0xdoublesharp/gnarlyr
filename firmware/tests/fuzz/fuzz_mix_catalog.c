#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuzz_source.h"
#include "playback/mix_catalog.h"
#include "playback/mix_identity.h"

enum {
    ACTION_LIMIT = 96,
};

typedef struct FuzzDisk {
    uint8_t bytes[MIX_CATALOG_BYTES];
    uint32_t operation;
    uint32_t fail_at;
    uint8_t failure;
    bool faults;
    bool invalid_io;
} FuzzDisk;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool fail_now(FuzzDisk *disk)
{
    bool fail = disk->faults && disk->failure != 0 && disk->operation == disk->fail_at;
    disk->operation++;
    return fail;
}

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    FuzzDisk *disk = context;
    if (offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        disk->invalid_io = true;
        return false;
    }
    if (fail_now(disk)) {
        return false;
    }
    memcpy(bytes, disk->bytes + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    FuzzDisk *disk = context;
    if (offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        disk->invalid_io = true;
        return false;
    }
    bool fail = fail_now(disk);
    uint32_t written = fail && disk->failure == 2 ? length / 2 : length;
    memcpy(disk->bytes + offset, bytes, written);
    if (fail && disk->failure == 3 && written > 0) {
        disk->bytes[offset + written / 2] ^= 1;
    }
    return !fail || disk->failure == 3;
}

static StoreFile file_of(FuzzDisk *disk)
{
    return (StoreFile){
        .source = {read_disk, disk}, .write = write_disk, .size = sizeof disk->bytes};
}

static MixPatch patch_of(uint8_t first, uint8_t second)
{
    MixPatch patch = {.slot = first % (MIX_CATALOG_SLOTS + 2)};
    if ((second & 1u) != 0) {
        patch.entry.active = true;
        (void)snprintf(patch.entry.name, sizeof patch.entry.name, "Mix %u", first);
        patch.entry.generation = ((uint32_t)first << 8) | second;
        patch.entry.count = (uint16_t)((second >> 1) % 8);
        patch.entry.bytes = (uint32_t)patch.entry.count * MIX_RECORD_MIN;
        patch.entry.file_bank = (second >> 5) & 1u;
    }
    if ((second & 0x40u) != 0) {
        patch.entry.bytes++;
    }
    if ((second & 0x80u) != 0) {
        patch.entry.name[0] = '\a';
    }
    return patch;
}

static void check_catalog(const MixCatalog *catalog)
{
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixEntry entry;
        if (mix_catalog_read(catalog, slot, &entry) && entry.active) {
            fuzz_require(memchr(entry.name, '\0', sizeof entry.name) != NULL);
            fuzz_require(entry.file_bank <= 1);
            fuzz_require(entry.bytes >= (uint32_t)entry.count * MIX_RECORD_MIN &&
                         entry.bytes <= (uint32_t)entry.count * MIX_RECORD_MAX);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzDisk disk = {0};
    StoreFile file = file_of(&disk);
    MixCatalog catalog;
    mix_catalog_open(&catalog, file);
    MixCatalogJob job = {0};
    bool active = false;
    size_t limit = size < ACTION_LIMIT ? size : ACTION_LIMIT;
    for (size_t index = 0; index < limit; index++) {
        uint8_t value = data[index];
        uint8_t next = index + 1 < size ? data[index + 1] : 0;
        switch (value >> 5) {
        case 0:
            if (active) {
                mix_catalog_abort(&job);
                active = false;
            }
            mix_catalog_open(&catalog, file);
            break;
        case 1: {
            MixEntry entry;
            mix_catalog_read(&catalog, value % (MIX_CATALOG_SLOTS + 2), &entry);
            break;
        }
        case 2:
            if (!active) {
                mix_catalog_previous(&catalog);
            }
            break;
        case 3:
            if (!active) {
                uint32_t revision = (value & 1u) != 0 ? catalog.generation : catalog.generation + 1;
                active = mix_catalog_begin(&job, &catalog, revision, patch_of(value, next));
            }
            break;
        case 4:
            if (active && mix_catalog_step(&job) != STORE_WORKING) {
                active = false;
            }
            break;
        case 5:
            if (active) {
                mix_catalog_abort(&job);
                active = false;
            }
            break;
        case 6:
            if (!active) {
                disk.bytes[((uint32_t)value * 257u + next) % sizeof disk.bytes] ^= next | 1u;
            }
            break;
        case 7:
            disk.faults = (value & 1u) != 0;
            disk.failure = (uint8_t)(1 + next % 3);
            disk.fail_at = disk.operation + ((value >> 1) & 7u);
            break;
        }
        fuzz_require(catalog.busy == active);
        fuzz_require(!disk.invalid_io);
    }
    if (active) {
        mix_catalog_abort(&job);
    }
    disk.faults = false;
    mix_catalog_open(&catalog, file);
    check_catalog(&catalog);
    fuzz_require(!disk.invalid_io);
    return 0;
}
