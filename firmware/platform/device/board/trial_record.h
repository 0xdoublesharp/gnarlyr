#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/fat.h"
#include "device/update_trial.h"
#include "device/updater.h"

typedef struct TrialDisk {
    FatVolume *volume;
    UpdateStorage storage;
} TrialDisk;

bool trial_record_open(TrialDisk *disk);
bool trial_record_store(FatVolume *volume, const uint8_t *bytes);
bool trial_record_copy(const TrialDisk *disk, uint32_t from, uint32_t to, uint32_t *buffer,
                       UpdateProgress progress);
