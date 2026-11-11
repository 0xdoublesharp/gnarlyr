#pragma once

#include <stdint.h>

#include "hal/storage.h"
#include "library/cover_build.h"
#include "library/library_scan.h"

typedef struct RescanOutcome {
    ScanResult result;
    ScanStats stats;
    ScanResult cover_result;
    CoverStats covers;
} RescanOutcome;

typedef struct RescanProgress {
    void (*tracks)(uint32_t files, uint32_t tracks);
    void (*covers)(uint32_t done, uint32_t total);
} RescanProgress;

#define RESCAN_MODULE_MAGIC 0x4E414353u

typedef struct RescanModuleEntry {
    uint32_t magic;
    RescanOutcome (*run)(uint32_t capacity, HalFile scratch, RescanProgress progress,
                         uint32_t stops);
} RescanModuleEntry;

extern const RescanModuleEntry RESCAN_MODULE_ENTRY;
