#pragma once

#include <stdbool.h>

#include "core/i18n.h"
#include "hal/language.h"
#include "hal/storage.h"

typedef struct LanguageSource {
    bool from_file;
    HalFile file;
    HalLanguagePack pack;
} LanguageSource;

typedef struct LanguageState {
    LanguageSource current;
    uint8_t *module_area;
    uint32_t module_room;
    uint32_t shown[STRING_SEGMENT_COUNT];
} LanguageState;

extern LanguageState language_state;

bool language_source_info(const LanguageSource *source, I18nPackInfo *info);
bool language_source_open(const char *code, LanguageSource *source);
