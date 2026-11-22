#pragma once

#include <stdint.h>

#include "language.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    LANGUAGE_LABEL_BYTES = 32,
    LANGUAGE_NOTICE_BYTES = 128
};

typedef struct LanguagePickerConfig {
    const LanguageList *languages;
    void (*choose)(void *context, int index);
    void *context;
    const Theme *theme;
} LanguagePickerConfig;

typedef struct LanguagePicker {
    Screen screen;
    LanguagePickerConfig config;
    ListModel model;
    ListView list;
    int explained;
    uint32_t now_ms;
    char labels[LANGUAGE_CAPACITY][LANGUAGE_LABEL_BYTES];
} LanguagePicker;

Screen *language_picker_init(LanguagePicker *picker, LanguagePickerConfig config);
const char *language_exonym(const char *code);
const char *language_picker_notice(const LanguagePicker *picker, char *text, uint32_t capacity);
