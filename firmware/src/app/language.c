#include "language.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "hal/system.h"
#include "language_source.h"

enum {
    PATH_CAPACITY = 48,
    MODULE_ALIGN = 4
};

const char LANGUAGE_FOLDER[] = "/" FIRMWARE_DATA_DIRECTORY "/lang";
static const char SUFFIX[] = ".pack";

LanguageState language_state;

static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static bool source_read(const LanguageSource *source, uint32_t offset, void *buffer,
                        uint32_t length)
{
    return source->from_file ? hal_file_read(source->file, offset, buffer, length)
                             : hal_language_read(source->pack, offset, buffer, length);
}

bool language_source_info(const LanguageSource *source, I18nPackInfo *info)
{
    uint8_t index[I18N_INDEX_BYTES];
    return source_read(source, 0, index, sizeof index) && i18n_pack_info(index, sizeof index, info);
}

static bool source_attach(const LanguageSource *source, const char *code)
{
    I18nPackInfo info;
    uint32_t capacity = 0;
    uint8_t *memory = hal_language_memory(&capacity);
    uint32_t start = 0;
    if (!language_source_info(source, &info) ||
        (start = (info.resident + MODULE_ALIGN - 1) & ~(uint32_t)(MODULE_ALIGN - 1)) > capacity ||
        (code != NULL && !same_text(info.code, code)) ||
        (source->from_file && source->file.size != info.size)) {
        return false;
    }
    i18n_detach();
    language_state.current = *source;
    language_state.module_area = memory + start;
    language_state.module_room = capacity - start;
    return source_read(source, 0, memory, info.resident) && i18n_attach(memory, info.resident);
}

static bool file_path(const char *code, char *path)
{
    StringBuilder builder = string_builder_make(path, PATH_CAPACITY);
    string_builder_append(&builder, LANGUAGE_FOLDER);
    string_builder_append_char(&builder, '/');
    string_builder_append(&builder, code);
    string_builder_append(&builder, SUFFIX);
    return !builder.overflowed;
}

bool language_source_open(const char *code, LanguageSource *source)
{
    char path[PATH_CAPACITY];
    *source = (LanguageSource){.from_file = true};
    return file_path(code, path) && hal_file_open(&source->file, path);
}

static bool attach_file(const char *code)
{
    LanguageSource source;
    if (!language_source_open(code, &source)) {
        return false;
    }
    bool attached = source_attach(&source, code);
    hal_file_close(&source.file);
    return attached;
}

static bool attach_built_in(uint8_t copy, const char *code)
{
    for (uint8_t index = 0; index < LANGUAGE_CAPACITY; index++) {
        LanguageSource source = {.pack = {.copy = copy, .index = index}};
        I18nPackInfo info;
        if (!language_source_info(&source, &info)) {
            return false;
        }
        if (code == NULL || same_text(info.code, code)) {
            return source_attach(&source, code);
        }
    }
    return false;
}

static bool attach_any_copy(const char *code)
{
    for (uint8_t copy = 0; copy < HAL_LANGUAGE_COPIES; copy++) {
        if (attach_built_in(copy, code)) {
            return true;
        }
    }
    return false;
}

bool language_load(const char *code)
{
    if (hal_system_restart_pending()) {
        return i18n_attached();
    }
    bool chosen = code[0] != '\0';
    if ((chosen && (attach_file(code) || attach_any_copy(code))) || attach_any_copy(NULL)) {
        return true;
    }
    return i18n_attached();
}

bool language_is(const char *code)
{
    return same_text(i18n_code(), code);
}
