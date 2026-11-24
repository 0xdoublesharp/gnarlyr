#include <stddef.h>

#include "board.h"
#include "core/drive_path.h"
#include "core/seam.h"
#include "device/fat.h"
#include "device/storage_mount.h"
#include "emmc.h"
#include "gfx/font_pack.h"
#include "hal/storage.h"
#include "internal_volume.h"
#include "sd.h"
#include "storage_listing.h"
#include "storage_reset.h"

enum {
    OPEN_FILE_CAPACITY = 9,
    FILE_EPOCH_STEP = 16,
    OPEN_DIR_CAPACITY = HAL_OPEN_DIRS
};

_Static_assert((int)HAL_NAME_CAPACITY == (int)FAT_NAME_CAPACITY, "listed names fit the HAL entry");
_Static_assert(OPEN_FILE_CAPACITY <= FILE_EPOCH_STEP, "each slot fits the handle");

static bool mount_tried;
static StorageKind mounted_kind;
static FatVolume volume;
static FatFile files[OPEN_FILE_CAPACITY];
static FatExtents extents[OPEN_FILE_CAPACITY];
static FatVolume *file_volumes[OPEN_FILE_CAPACITY];
static uint32_t file_epoch;
static FatDirCursor dirs[OPEN_DIR_CAPACITY];
static FatVolume *dir_volumes[OPEN_DIR_CAPACITY];
static bool dir_in_use[OPEN_DIR_CAPACITY];
static FatDirEntry listed;
static FatVolume *listed_volume;

static bool mounted(void)
{
    if (!mount_tried) {
        mount_tried = true;
        mounted_kind = storage_mount(&volume, sd_start, emmc_volume_start);
    }
    return mounted_kind != STORAGE_NONE;
}

CORE_COLD HalVolume hal_file_volume(void)
{
    if (!mounted()) {
        return HAL_VOLUME_NONE;
    }
    return mounted_kind == STORAGE_CARD ? HAL_VOLUME_CARD : HAL_VOLUME_INTERNAL;
}

CORE_COLD bool hal_file_combined(void)
{
    return hal_file_volume() == HAL_VOLUME_CARD && internal_volume_mount() != NULL;
}

static int free_slot(void)
{
    for (int slot = 0; slot < OPEN_FILE_CAPACITY; slot++) {
        if (file_volumes[slot] == NULL) {
            return slot;
        }
    }
    return -1;
}

static uint32_t file_slot(HalFile file)
{
    return (uint32_t)file.handle ^ file_epoch;
}

static FatVolume *player_volume(void)
{
    if (mounted() && mounted_kind == STORAGE_INTERNAL) {
        return &volume;
    }
    return internal_volume_mount();
}

FatVolume *board_route_volume(const char **path)
{
    const char *internal = drive_path_internal(*path);
    if (internal == NULL) {
        return mounted() ? &volume : NULL;
    }
    *path = internal;
    return player_volume();
}

bool hal_file_open(HalFile *file, const char *path)
{
    *file = (HalFile){.handle = -1, .size = 0};
    int slot = free_slot();
    FatVolume *target = board_route_volume(&path);
    if (slot < 0 || target == NULL || !fat_open(target, path, &files[slot])) {
        return false;
    }
    fat_attach_extents(&files[slot], &extents[slot]);
    file_volumes[slot] = target;
    uint32_t handle = file_epoch | (uint32_t)slot;
    *file = (HalFile){.handle = (int32_t)handle, .size = files[slot].size};
    return true;
}

bool hal_file_read(HalFile file, uint32_t offset, void *buffer, uint32_t length)
{
    uint32_t slot = file_slot(file);
    return slot < OPEN_FILE_CAPACITY && file_volumes[slot] != NULL &&
           fat_read(file_volumes[slot], &files[slot], offset, buffer, length);
}

void hal_file_close(HalFile *file)
{
    uint32_t slot = file_slot(*file);
    if (slot < OPEN_FILE_CAPACITY) {
        file_volumes[slot] = NULL;
    }
    file->handle = -1;
}

__attribute__((noinline)) static bool write_open_file(FatVolume *target, FatFile *file,
                                                      uint32_t offset, const void *bytes,
                                                      uint32_t length)
{
    board_start_record_step(HAL_START_STEP_CARD_WRITE);
    bool written = fat_write(target, file, offset, bytes, length);
    board_start_record_step_done();
    return written;
}

CORE_COLD static bool write_in(FatVolume *target, const char *path, uint32_t offset,
                               const void *bytes, uint32_t length)
{
    FatFile file;
    return target != NULL && fat_open(target, path, &file) &&
           write_open_file(target, &file, offset, bytes, length);
}

CORE_COLD bool hal_file_write(const char *path, const void *bytes, uint32_t length)
{
    FatVolume *target = board_route_volume(&path);
    return write_in(target, path, 0, bytes, length);
}

CORE_COLD bool hal_file_write_at(const char *path, uint32_t offset, const void *bytes,
                                 uint32_t length)
{
    FatVolume *target = board_route_volume(&path);
    return write_in(target, path, offset, bytes, length);
}

bool hal_file_write_open(HalFile file, uint32_t offset, const void *bytes, uint32_t length)
{
    uint32_t slot = file_slot(file);
    return slot < OPEN_FILE_CAPACITY && file_volumes[slot] != NULL &&
           write_open_file(file_volumes[slot], &files[slot], offset, bytes, length);
}

static BlockDevice card_device;

static bool read_and_beat(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    board_stall_beat();
    return card_device.read(context, lba, count, buffer);
}

static HalReserveResult reserve_result(FatReserveResult result)
{
    switch (result) {
    case FAT_RESERVE_EXISTS:
        return HAL_RESERVE_EXISTS;
    case FAT_RESERVE_CREATED:
        return HAL_RESERVE_CREATED;
    case FAT_RESERVE_NO_SPACE:
        return HAL_RESERVE_NO_SPACE;
    case FAT_RESERVE_FAILED:
        return HAL_RESERVE_FAILED;
    }
    return HAL_RESERVE_FAILED;
}

CORE_COLD static HalReserveResult reserve_in(FatVolume *target, const char *path, uint32_t size)
{
    if (target == NULL) {
        return HAL_RESERVE_FAILED;
    }
    card_device = target->device;
    target->device.read = read_and_beat;
    board_start_record_step(HAL_START_STEP_CARD_WRITE);
    FatReserveResult result = fat_reserve(target, path, size);
    board_start_record_step_done();
    target->device = card_device;
    return reserve_result(result);
}

CORE_COLD HalReserveResult hal_file_reserve(const char *path, uint32_t size)
{
    FatVolume *target = board_route_volume(&path);
    return reserve_in(target, path, size);
}

CORE_COLD bool hal_internal_file_read(const char *path, void *buffer, uint32_t length)
{
    FatVolume *target = player_volume();
    FatFile file;
    return target != NULL && fat_open(target, path, &file) && file.size >= length &&
           fat_read(target, &file, 0, buffer, length);
}

CORE_COLD bool hal_internal_file_write(const char *path, const void *bytes, uint32_t length)
{
    return write_in(player_volume(), path, 0, bytes, length);
}

CORE_COLD bool hal_internal_file_write_at(const char *path, uint32_t offset, const void *bytes,
                                          uint32_t length)
{
    return write_in(player_volume(), path, offset, bytes, length);
}

CORE_COLD HalReserveResult hal_internal_file_reserve(const char *path, uint32_t size)
{
    return reserve_in(player_volume(), path, size);
}

CORE_SEAM CORE_COLD void storage_reset(void)
{
    font_pack_detach_extra();
    mount_tried = false;
    internal_volume_reset();
    file_epoch = (file_epoch + FILE_EPOCH_STEP) & INT32_MAX;
    for (int slot = 0; slot < OPEN_FILE_CAPACITY; slot++) {
        file_volumes[slot] = NULL;
    }
    for (int slot = 0; slot < OPEN_DIR_CAPACITY; slot++) {
        dir_in_use[slot] = false;
    }
}

static int free_dir_slot(void)
{
    for (int slot = 0; slot < OPEN_DIR_CAPACITY; slot++) {
        if (!dir_in_use[slot]) {
            return slot;
        }
    }
    return -1;
}

static FatVolume *open_directory(int slot, const char *path)
{
    if (path == NULL) {
        dirs[slot] = (FatDirCursor){.directory = listed.file};
        return listed.file.directory ? listed_volume : NULL;
    }
    FatVolume *target = board_route_volume(&path);
    return target != NULL && fat_open_directory(target, path, &dirs[slot]) ? target : NULL;
}

CORE_COLD bool hal_dir_open(HalDir *dir, const char *path)
{
    dir->handle = -1;
    int slot = free_dir_slot();
    if (slot < 0 || !mounted()) {
        return false;
    }
    dir_volumes[slot] = open_directory(slot, path);
    if (dir_volumes[slot] == NULL) {
        return false;
    }
    dir_in_use[slot] = true;
    dir->handle = slot;
    return true;
}

void hal_storage_lend_cache(uint32_t *window, uint32_t sectors)
{
    fat_lend_window(&volume, window, sectors);
}

FatDirEntry *storage_listing_begin(const HalDir *dir, FatVolume **mounted_volume,
                                   FatDirCursor **cursor)
{
    if (dir->handle < 0 || dir->handle >= OPEN_DIR_CAPACITY || !dir_in_use[dir->handle]) {
        return NULL;
    }
    listed_volume = dir_volumes[dir->handle];
    *mounted_volume = listed_volume;
    *cursor = &dirs[dir->handle];
    return &listed;
}

void hal_dir_close(HalDir *dir)
{
    if (dir->handle >= 0 && dir->handle < OPEN_DIR_CAPACITY) {
        dir_in_use[dir->handle] = false;
    }
    dir->handle = -1;
}

CORE_COLD FatVolume *board_music_volume(void)
{
    return mounted() ? &volume : NULL;
}
