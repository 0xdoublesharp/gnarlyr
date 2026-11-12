#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library_builder.h"
#include "library_store.h"

typedef struct HiddenScan {
    uint32_t keys[HIDDEN_TRACKS_RECORDS];
    uint16_t records;
} HiddenScan;

void hidden_scan_load(HiddenScan *scan);
bool hidden_scan_skips(void *context, const BuilderTrack *track);
void hidden_scan_forget_runtime(void);
