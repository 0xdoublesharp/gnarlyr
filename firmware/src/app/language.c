#include "language.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "hal/language.h"
#include "hal/storage.h"
#include "hal/system.h"

enum {
    PATH_CAPACITY = 48
};

const char LANGUAGE_FOLDER[] = "/" FIRMWARE_DATA_DIRECTORY "/lang";
static const char SUFFIX[] = ".pack";

typedef struct Source {
    bool from_file;
    HalFile file;
    HalLanguagePack pack;
} Source;

static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static bool source_read(const Source *source, uint32_t offset, void *buffer, uint32_t length)
{
    return source->from_file ? hal_file_read(source->file, offset, buffer, length)
                             : hal_language_read(source->pack, offset, buffer, length);
}

static bool source_info(const Source *source, I18nPackInfo *info)
{
    uint8_t header[I18N_HEADER_BYTES];
    return source_read(source, 0, header, sizeof header) &&
           i18n_pack_info(header, sizeof header, info);
}

static bool source_attach(const Source *source, const char *code)
{
    I18nPackInfo info;
    uint32_t capacity = 0;
    uint8_t *memory = hal_language_memory(&capacity);
    if (!source_info(source, &info) || info.size > capacity ||
        (code != NULL && !same_text(info.code, code))) {
        return false;
    }
    i18n_detach();
    return source_read(source, 0, memory, info.size) && i18n_attach(memory, info.size);
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

static bool open_file(const char *code, Source *source)
{
    char path[PATH_CAPACITY];
    *source = (Source){.from_file = true};
    return file_path(code, path) && hal_file_open(&source->file, path);
}

static bool attach_file(const char *code)
{
    Source source;
    if (!open_file(code, &source)) {
        return false;
    }
    bool attached = source_attach(&source, code);
    hal_file_close(&source.file);
    return attached;
}

static bool attach_built_in(uint8_t copy, const char *code)
{
    for (uint8_t index = 0; index < LANGUAGE_CAPACITY; index++) {
        Source source = {.pack = {.copy = copy, .index = index}};
        I18nPackInfo info;
        if (!source_info(&source, &info)) {
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

int language_list_find(const LanguageList *list, const char *code)
{
    for (int index = 0; index < list->count; index++) {
        if (same_text(list->codes[index], code)) {
            return index;
        }
    }
    return -1;
}

static void list_put(LanguageList *list, const I18nPackInfo *info)
{
    int index = language_list_find(list, info->code);
    if (index < 0 && list->count < LANGUAGE_CAPACITY) {
        index = list->count++;
    }
    if (index < 0) {
        return;
    }
    for (int byte = 0; byte < I18N_CODE_BYTES; byte++) {
        list->codes[index][byte] = info->code[byte];
    }
    for (int byte = 0; byte < I18N_NAME_BYTES; byte++) {
        list->names[index][byte] = info->name[byte];
    }
    list->options[index] = list->names[index];
}

static void list_built_in(LanguageList *list)
{
    for (uint8_t copy = 0; copy < HAL_LANGUAGE_COPIES && list->count == 0; copy++) {
        for (uint8_t index = 0; index < LANGUAGE_CAPACITY; index++) {
            Source source = {.pack = {.copy = copy, .index = index}};
            I18nPackInfo info;
            if (!source_info(&source, &info)) {
                break;
            }
            list_put(list, &info);
        }
    }
}

static bool code_of_file(const char *name, char *code)
{
    int length = 0;
    while (length < I18N_CODE_BYTES - 1 && name[length] != '\0' && name[length] != '.') {
        code[length] = name[length];
        length++;
    }
    code[length] = '\0';
    return length > 0 && same_text(name + length, SUFFIX);
}

static void list_file(LanguageList *list, const char *name)
{
    char code[I18N_CODE_BYTES];
    Source source;
    I18nPackInfo info;
    if (!code_of_file(name, code) || !open_file(code, &source)) {
        return;
    }
    if (source_info(&source, &info) && same_text(info.code, code)) {
        list_put(list, &info);
    }
    hal_file_close(&source.file);
}

static void list_files(LanguageList *list)
{
    HalDir dir;
    HalDirEntry entry;
    if (!hal_dir_open(&dir, LANGUAGE_FOLDER)) {
        return;
    }
    while (hal_dir_next(&dir, &entry)) {
        if (!entry.directory) {
            list_file(list, entry.name);
        }
    }
    hal_dir_close(&dir);
}

void language_list(LanguageList *list)
{
    list->count = 0;
    list_built_in(list);
    list_files(list);
}
