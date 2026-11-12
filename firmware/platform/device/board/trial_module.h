#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/updater.h"

#define TRIAL_BOOT_MODULE_NUMBER 15u

struct UsbShared;

typedef struct TrialCalls {
    bool (*install)(const UpdateFile *update, uint32_t *buffer, UpdateProgress progress, bool both);
} TrialCalls;

typedef struct TrialBootModuleEntry {
    uint32_t magic;
    void (*start)(struct UsbShared *shared, uint32_t *buffer);
    void (*session_start)(void);
    void (*session_end)(struct UsbShared *shared, uint32_t *buffer);
} TrialBootModuleEntry;

extern const TrialCalls TRIAL_CALLS;
extern const TrialBootModuleEntry TRIAL_BOOT_MODULE_ENTRY;

const TrialCalls *board_trial_calls(void);
void trial_boot_module_session_start(void);
bool trial_module_install(const UpdateFile *update, uint32_t *buffer, UpdateProgress progress,
                          bool both);
void trial_boot_module_start(struct UsbShared *shared, uint32_t *buffer);
void trial_boot_module_session_end(struct UsbShared *shared, uint32_t *buffer);
