#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device/updater.h"
#include "playback/store.h"
#include "trial_record.h"

#define TRIAL_BOOT_MODULE_NUMBER 15u

struct UsbShared;
struct HeadphoneStore;
struct SettingsStore;
struct Player;
struct Favorites;
struct Bookmarks;
struct BluetoothSaved;
struct CardFilesReport;

typedef struct TrialBootModuleEntry {
    uint32_t magic;
    void (*start)(struct UsbShared *shared, uint32_t *buffer);
    void (*session_end)(struct UsbShared *shared, uint32_t *buffer, const uint32_t *running);
    bool (*settle)(struct UsbShared *shared);
    void (*headphones_load)(struct HeadphoneStore *store);
    void (*settings_read)(struct SettingsStore *store);
    bool (*player_restore)(struct Player *player, bool cold_start);
    StoreLoad (*favorites_read)(struct Favorites *favorites);
    StoreLoad (*spoken_read)(struct Bookmarks *bookmarks);
    void (*bluetooth_load)(struct BluetoothSaved *saved);
    bool (*card_files_prepare)(size_t *prepare_index, struct CardFilesReport *report, bool charging,
                               bool *finished);
} TrialBootModuleEntry;

typedef struct TrialInstall {
    TrialDisk disk;
    const UpdateFile *update;
    uint32_t *buffer;
    UpdateProgress progress;
} TrialInstall;

extern const TrialBootModuleEntry TRIAL_BOOT_MODULE_ENTRY;

bool trial_module_install(const TrialInstall *install, bool both);
void trial_boot_module_start(struct UsbShared *shared, uint32_t *buffer);
void trial_boot_module_session_end(struct UsbShared *shared, uint32_t *buffer,
                                   const uint32_t *running);
bool user_state_module_settle(struct UsbShared *shared);
