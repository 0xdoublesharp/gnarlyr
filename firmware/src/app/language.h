#pragma once

#include <stdbool.h>

#include "core/i18n.h"

enum {
    LANGUAGE_CAPACITY = 8
};

typedef struct LanguageList {
    char codes[LANGUAGE_CAPACITY][I18N_CODE_BYTES];
    char names[LANGUAGE_CAPACITY][I18N_NAME_BYTES];
    const char *options[LANGUAGE_CAPACITY];
    int count;
} LanguageList;

extern const char LANGUAGE_FOLDER[];

bool language_load(const char *code);
bool language_use_module(uint32_t module);
bool language_is(const char *code);
void language_list(LanguageList *list);
bool language_file_sound(const char *code);
int language_list_find(const LanguageList *list, const char *code);
