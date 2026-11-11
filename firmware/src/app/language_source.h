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

bool language_source_info(const LanguageSource *source, I18nPackInfo *info);
bool language_source_open(const char *code, LanguageSource *source);
