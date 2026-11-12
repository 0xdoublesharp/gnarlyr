#include "language.h"

#include "core/hash.h"
#include "language_source.h"

enum {
    CHUNK_BYTES = 128,
    MODULE_ALIGN = 4
};

static const char SUFFIX[] = ".pack";

static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
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
            LanguageSource source = {.pack = {.copy = copy, .index = index}};
            I18nPackInfo info;
            if (!language_source_info(&source, &info)) {
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

static bool segment_sound(HalFile file, const I18nSegment *row)
{
    uint8_t chunk[CHUNK_BYTES];
    uint32_t hash = HASH_START;
    for (uint32_t done = 0; done < row->size; done += CHUNK_BYTES) {
        uint32_t length = row->size - done < CHUNK_BYTES ? row->size - done : CHUNK_BYTES;
        if (!hal_file_read(file, row->offset + done, chunk, length)) {
            return false;
        }
        hash = hash_update(hash, chunk, length);
    }
    return hash == row->checksum;
}

static bool file_sound(HalFile file, const char *code, I18nPackInfo *info)
{
    uint8_t index[I18N_INDEX_BYTES];
    uint32_t capacity = 0;
    hal_language_memory(&capacity);
    if (!hal_file_read(file, 0, index, sizeof index) ||
        !i18n_pack_info(index, sizeof index, info) || !same_text(info->code, code) ||
        info->size != file.size) {
        return false;
    }
    uint32_t start = (info->resident + MODULE_ALIGN - 1) & ~(uint32_t)(MODULE_ALIGN - 1);
    for (uint32_t segment = 1; segment < STRING_SEGMENT_COUNT; segment++) {
        I18nSegment row;
        if (start > capacity || !i18n_pack_segment(index, segment, &row) ||
            row.size > capacity - start || !segment_sound(file, &row)) {
            return false;
        }
    }
    return true;
}

bool language_file_sound(const char *code)
{
    LanguageSource source;
    I18nPackInfo info;
    if (!language_source_open(code, &source)) {
        return true;
    }
    bool sound = file_sound(source.file, code, &info);
    hal_file_close(&source.file);
    return sound;
}

static void list_file(LanguageList *list, const char *name)
{
    char code[I18N_CODE_BYTES];
    LanguageSource source;
    I18nPackInfo info;
    if (!code_of_file(name, code) || !language_source_open(code, &source)) {
        return;
    }
    if (file_sound(source.file, code, &info)) {
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
