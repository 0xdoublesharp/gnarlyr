#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/fat.h"
#include "fuzz_source.h"

enum {
    HEADER_BYTES = 12,
    ENTRY_BYTES = 4 + FAT_SECTOR_SIZE,
    OVERLAY_SECTORS = 32,
    READ_BYTES = 1024,
    DIRECTORY_LIMIT = 16
};

typedef struct SparseView {
    const uint8_t *bytes;
    size_t size;
    uint32_t sectors;
    uint32_t entries;
    uint32_t overlay_lba[OVERLAY_SECTORS];
    uint8_t overlay[OVERLAY_SECTORS][FAT_SECTOR_SIZE];
    uint32_t overlay_count;
    bool read_only;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t fail_read;
    uint32_t fail_write;
} SparseView;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool sparse_open(SparseView *view, const uint8_t *data, size_t size)
{
    if (size < HEADER_BYTES || memcmp(data, "DSSP", 4) != 0) {
        return false;
    }
    uint32_t entries = get32(data + 8);
    if (entries > (size - HEADER_BYTES) / ENTRY_BYTES) {
        return false;
    }
    *view = (SparseView){
        .bytes = data,
        .size = size,
        .sectors = get32(data + 4),
        .entries = entries,
    };
    return view->sectors != 0;
}

static const uint8_t *overlay_sector(const SparseView *view, uint32_t lba)
{
    for (uint32_t index = 0; index < view->overlay_count; index++) {
        if (view->overlay_lba[index] == lba) {
            return view->overlay[index];
        }
    }
    return NULL;
}

static const uint8_t *sparse_sector(const SparseView *view, uint32_t lba)
{
    for (uint32_t index = view->entries; index > 0; index--) {
        const uint8_t *entry = view->bytes + HEADER_BYTES + (size_t)(index - 1) * ENTRY_BYTES;
        if (get32(entry) == lba) {
            return entry + 4;
        }
    }
    return NULL;
}

static bool read_sectors(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    SparseView *view = context;
    view->read_calls++;
    if (view->read_calls == view->fail_read) {
        return false;
    }
    if (lba >= view->sectors || count > view->sectors - lba) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint32_t index = 0; index < count; index++) {
        const uint8_t *bytes = overlay_sector(view, lba + index);
        if (bytes == NULL) {
            bytes = sparse_sector(view, lba + index);
        }
        if (bytes == NULL) {
            memset(out + (size_t)index * FAT_SECTOR_SIZE, 0, FAT_SECTOR_SIZE);
        } else {
            memcpy(out + (size_t)index * FAT_SECTOR_SIZE, bytes, FAT_SECTOR_SIZE);
        }
    }
    return true;
}

static uint8_t *write_sector(SparseView *view, uint32_t lba)
{
    for (uint32_t index = 0; index < view->overlay_count; index++) {
        if (view->overlay_lba[index] == lba) {
            return view->overlay[index];
        }
    }
    if (view->overlay_count == OVERLAY_SECTORS) {
        return NULL;
    }
    uint32_t index = view->overlay_count++;
    view->overlay_lba[index] = lba;
    return view->overlay[index];
}

static bool write_sectors(void *context, uint32_t lba, uint32_t count, const void *buffer)
{
    SparseView *view = context;
    view->write_calls++;
    if (view->write_calls == view->fail_write) {
        return false;
    }
    if (view->read_only || lba >= view->sectors || count > view->sectors - lba ||
        count > OVERLAY_SECTORS) {
        return false;
    }
    const uint8_t *bytes = buffer;
    for (uint32_t index = 0; index < count; index++) {
        uint8_t *sector = write_sector(view, lba + index);
        if (sector == NULL) {
            return false;
        }
        memcpy(sector, bytes + (size_t)index * FAT_SECTOR_SIZE, FAT_SECTOR_SIZE);
    }
    return true;
}

static BlockDevice block_device(SparseView *view)
{
    return (BlockDevice){read_sectors, view, write_sectors};
}

static void exercise_directory(FatVolume *volume, const char *path)
{
    FatDirCursor cursor;
    if (!fat_open_directory(volume, path, &cursor)) {
        return;
    }
    FatDirEntry entry;
    for (int index = 0; index < DIRECTORY_LIMIT && fat_next_entry(volume, &cursor, &entry);
         index++) {
        fuzz_require(entry.name[sizeof entry.name - 1] == '\0' ||
                     memchr(entry.name, '\0', sizeof entry.name) != NULL);
    }
}

static void exercise_file(FatVolume *volume, const char *path, uint8_t control)
{
    FatFile file;
    if (!fat_open(volume, path, &file)) {
        return;
    }
    uint8_t bytes[READ_BYTES];
    uint32_t length = file.size < sizeof bytes ? file.size : sizeof bytes;
    uint32_t offset = file.size > length ? (uint32_t)control * (file.size - length) / UINT8_MAX : 0;
    fat_read(volume, &file, offset, bytes, length);
    uint32_t lba = 0;
    if (file.size != 0) {
        fat_file_sector(volume, &file, offset / FAT_SECTOR_SIZE, &lba);
    }
}

static void reset_device(SparseView *view, uint32_t fail_read, uint32_t fail_write, bool read_only)
{
    view->overlay_count = 0;
    view->read_calls = 0;
    view->write_calls = 0;
    view->fail_read = fail_read;
    view->fail_write = fail_write;
    view->read_only = read_only;
}

static void exercise_reserve(SparseView *view, const char *path, uint32_t size, uint32_t fail_read,
                             uint32_t fail_write, bool read_only)
{
    reset_device(view, fail_read, fail_write, read_only);
    FatVolume volume;
    if (!fat_mount(&volume, block_device(view))) {
        return;
    }
    FatReserveResult result = fat_reserve(&volume, path, size);
    fuzz_require(result >= FAT_RESERVE_EXISTS && result <= FAT_RESERVE_FAILED);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const char *const PATHS[] = {"/",           "/.existing", "/Music",
                                        "/README.TXT", "/notes.txt", "/missing"};
    static const char *const CREATE_PATHS[] = {
        "/fuzz.bin",
        "/.existing/fuzz.bin",
        "/fuzz long name.bin",
        "/bad:name.bin",
    };
    static const uint32_t CREATE_SIZES[] = {0, 1, 512, 4096, 8192, 65536};
    SparseView view;
    if (!sparse_open(&view, data, size)) {
        return 0;
    }
    uint8_t control = data[size - 1];
    reset_device(&view, 0, 0, false);
    FatVolume volume;
    if (!fat_mount(&volume, block_device(&view))) {
        return 0;
    }
    fuzz_require(volume.type == FAT_TYPE_16 || volume.type == FAT_TYPE_32 ||
                 volume.type == FAT_TYPE_EXFAT);
    fuzz_require(volume.cluster_count != 0);
    for (size_t index = 0; index < sizeof PATHS / sizeof PATHS[0]; index++) {
        exercise_directory(&volume, PATHS[index]);
        exercise_file(&volume, PATHS[index], control);
    }
    const char *created =
        CREATE_PATHS[(control >> 3) % (sizeof CREATE_PATHS / sizeof CREATE_PATHS[0])];
    uint32_t created_size =
        CREATE_SIZES[(control >> 5) % (sizeof CREATE_SIZES / sizeof CREATE_SIZES[0])];
    exercise_reserve(&view, "/.existing/library.db", 4096, 0, 0, false);
    exercise_reserve(&view, "/bad:name.bin", 4096, 0, 0, false);
    exercise_reserve(&view, created, created_size, 0, 0, false);
    exercise_reserve(&view, created, created_size, 0, 1u + (control & 7u), false);
    exercise_reserve(&view, created, created_size, 1u + (control >> 3 & 7u), 0, false);
    exercise_reserve(&view, created, created_size, 0, 0, true);
    return 0;
}
