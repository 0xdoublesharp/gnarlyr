#pragma once

#include "screen_modules.h"
#include "settings/parametric_file.h"

extern const char PARAMETRIC_STORE_PATH[];

typedef struct ParametricStore {
    ParametricFile file;
    bool uncertain;
} ParametricStore;

typedef struct ParametricModuleEntry {
    ScreenModuleEntry module;
    void (*load)(ParametricStore *store, ParametricEq *custom);
} ParametricModuleEntry;

void parametric_store_load(ParametricStore *store, ParametricEq *custom);
void parametric_store_read(ParametricStore *store, ParametricEq *custom);
ParametricBank *parametric_store_bank(void);
bool parametric_store_save(ParametricStore *store, const ParametricBank *bank);
