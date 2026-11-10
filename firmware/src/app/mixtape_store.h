#pragma once

#include "hal/storage.h"
#include "playback/mix_tape.h"

typedef struct MixtapeFile {
    char path[MIX_TAPE_PATH_BYTES];
    uint32_t size;
} MixtapeFile;

typedef enum MixtapeOperation {
    MIXTAPE_SAVE,
    MIXTAPE_RENAME,
    MIXTAPE_DELETE
} MixtapeOperation;

typedef struct MixtapeChange {
    MixtapeOperation operation;
    uint16_t slot;
    const char *name;
    MixSource source;
} MixtapeChange;

typedef struct MixtapeStore {
    MixtapeFile catalog_file;
    MixtapeFile data_file;
    MixCatalog catalog;
    MixEntry entry;
    MixTape tape;
    MixMeasure measured;
    union {
        MixCatalogJob catalog;
        MixTapeWrite writer;
        MixTapeCheck check;
    } job;
    uint16_t slot;
    uint8_t stage;
    bool recovered;
    StoreLoad load;
    StoreResult result;
} MixtapeStore;

StoreLoad mixtape_store_init(MixtapeStore *store);
bool mixtape_store_read(const MixtapeStore *store, uint16_t slot, MixEntry *entry);
bool mixtape_store_change(MixtapeStore *store, MixtapeChange change);
bool mixtape_store_open(MixtapeStore *store, uint16_t slot);
StoreResult mixtape_store_step(MixtapeStore *store);
void mixtape_store_cancel(MixtapeStore *store);
