#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct HalFile {
    int32_t handle;
    uint32_t size;
} HalFile;

typedef enum HalVolume {
    HAL_VOLUME_NONE,
    HAL_VOLUME_CARD,
    HAL_VOLUME_INTERNAL
} HalVolume;

HalVolume hal_file_volume(void);

bool hal_file_open(HalFile *file, const char *path);
bool hal_file_read(HalFile file, uint32_t offset, void *buffer, uint32_t length);
void hal_file_close(HalFile *file);
bool hal_file_remove(const char *path);
bool hal_file_write(const char *path, const void *bytes, uint32_t length);
bool hal_file_write_at(const char *path, uint32_t offset, const void *bytes, uint32_t length);
bool hal_file_write_open(HalFile file, uint32_t offset, const void *bytes, uint32_t length);

enum {
    HAL_RESERVE_CLEARED_BYTES = 4096
};

typedef enum HalReserveResult {
    HAL_RESERVE_EXISTS,
    HAL_RESERVE_CREATED,
    HAL_RESERVE_NO_SPACE,
    HAL_RESERVE_FAILED
} HalReserveResult;

HalReserveResult hal_file_reserve(const char *path, uint32_t size);

bool hal_internal_file_read(const char *path, void *buffer, uint32_t length);
bool hal_internal_file_write(const char *path, const void *bytes, uint32_t length);
bool hal_internal_file_write_at(const char *path, uint32_t offset, const void *bytes,
                                uint32_t length);
HalReserveResult hal_internal_file_reserve(const char *path, uint32_t size);

enum {
    HAL_NAME_CAPACITY = 768
};

typedef struct HalDir {
    int handle;
} HalDir;

typedef struct HalDirEntry {
    bool directory;
    bool hidden;
    uint32_t size;
    uint32_t modified;
    char name[HAL_NAME_CAPACITY];
} HalDirEntry;

bool hal_dir_open(HalDir *dir, const char *path);
bool hal_dir_next(HalDir *dir, HalDirEntry *entry);
void hal_dir_close(HalDir *dir);

typedef struct HalCardInfo {
    uint32_t sectors;
    uint32_t generation;
    bool present;
    bool needs_format;
    bool usb_busy;
} HalCardInfo;

typedef bool (*HalCardProgress)(void *context, uint32_t done, uint32_t total);

HalCardInfo hal_card_info(void);
bool hal_card_format(HalCardInfo expected, HalCardProgress progress, void *context);
