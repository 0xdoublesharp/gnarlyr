#include "theme_picker.h"

#include "core/string_builder.h"
#include "core/utf8.h"
#include "screens/listening_work.h"

static ThemeWork *const work = &listening_module_work.as.themes;

char *theme_catalog_detail(void)
{
    return work->detail;
}

ThemePack *theme_catalog_work(void)
{
    if (listening_module_work.owner != work) {
        listening_module_work.owner = work;
        *work = (ThemeWork){0};
    }
    return &work->pack;
}

ThemeCatalog *theme_picker_catalog(void)
{
    if (listening_module_work.owner != work) {
        listening_module_work.owner = work;
        *work = (ThemeWork){0};
    }
    if (!work->ready) {
        theme_catalog_scan(&work->catalog);
    }
    return &work->catalog;
}

bool theme_catalog_same_id(const uint8_t *first, const uint8_t *second)
{
    for (int at = 0; at < 16; at++) {
        if (first[at] != second[at]) {
            return false;
        }
    }
    return true;
}

static bool pack_name(const HalDirEntry *entry)
{
    if (entry->directory || entry->hidden || entry->name[0] == '.') {
        return false;
    }
    unsigned length = 0;
    while (entry->name[length] != '\0') {
        unsigned char value = (unsigned char)entry->name[length];
        if (length >= THEME_PACK_FILENAME_BYTES - 1 || value < 32 || value == '/' ||
            value == '\\') {
            return false;
        }
        length++;
    }
    static const char extension[] = ".gtheme";
    if (length <= sizeof extension - 1) {
        return false;
    }
    for (unsigned at = 0; at < sizeof extension; at++) {
        if (entry->name[length - (sizeof extension - 1) + at] != extension[at]) {
            return false;
        }
    }
    return true;
}

static void copy_text(char *destination, const char *source, unsigned capacity)
{
    StringBuilder builder = string_builder_make(destination, capacity);
    string_builder_append(&builder, source);
}

static bool read_pack(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    return hal_file_read(*(HalFile *)context, offset, bytes, length);
}

static ThemeCatalogEntry inspect(const char *filename)
{
    ThemeCatalogEntry entry = {0};
    copy_text(entry.filename, filename, sizeof entry.filename);
    copy_text(entry.name, filename, sizeof entry.name);
    char path[128];
    StringBuilder builder = string_builder_make(path, sizeof path);
    string_builder_append(&builder, THEME_PACK_FOLDER);
    string_builder_append(&builder, "/");
    string_builder_append(&builder, filename);
    HalFile file;
    if (!hal_file_open(&file, path)) {
        return entry;
    }
    entry.valid = theme_pack_open(&work->pack, (ByteSource){read_pack, &file}, file.size);
    hal_file_close(&file);
    if (entry.valid) {
        copy_text(entry.name, work->pack.name, sizeof entry.name);
        copy_text(entry.author, work->pack.author, sizeof entry.author);
        copy_text(entry.version, work->pack.version, sizeof entry.version);
        for (int at = 0; at < 16; at++) {
            entry.id[at] = work->pack.id[at];
        }
        entry.verified = work->pack.verified;
        entry.low_contrast = work->pack.low_contrast;
    }
    return entry;
}

static unsigned version_part(const char **text)
{
    unsigned value = 0;
    while (**text >= '0' && **text <= '9') {
        value = value < 65536 ? value * 10 + (unsigned)(**text - '0') : 65536;
        (*text)++;
    }
    if (**text == '.') {
        (*text)++;
    }
    return value;
}

static int compare(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return (unsigned char)*first - (unsigned char)*second;
}

static bool newer(const ThemeCatalogEntry *next, const ThemeCatalogEntry *previous)
{
    const char *first = next->version;
    const char *second = previous->version;
    for (int at = 0; at < 3; at++) {
        unsigned a = version_part(&first);
        unsigned b = version_part(&second);
        if (a != b) {
            return a > b;
        }
    }
    return compare(next->filename, previous->filename) < 0;
}

static void add_entry(ThemeCatalog *list, ThemeCatalogEntry entry)
{
    for (unsigned at = 0; entry.valid && at < list->count; at++) {
        if (list->entries[at].valid && theme_catalog_same_id(entry.id, list->entries[at].id)) {
            if (newer(&entry, &list->entries[at])) {
                list->entries[at] = entry;
            }
            return;
        }
    }
    if (list->count < THEME_PACK_MAX_INSTALLED) {
        list->entries[list->count++] = entry;
    }
}

void theme_catalog_scan(ThemeCatalog *list)
{
    if (list == &work->catalog) {
        work->ready = true;
    }
    list->count = 0;
    HalDir dir;
    if (!hal_dir_open(&dir, THEME_PACK_FOLDER)) {
        return;
    }
    HalDirEntry entry;
    for (unsigned seen = 0; seen < 128 && hal_dir_next(&dir, &entry); seen++) {
        if (pack_name(&entry)) {
            add_entry(list, inspect(entry.name));
        }
    }
    hal_dir_close(&dir);
    for (unsigned at = 1; at < list->count; at++) {
        ThemeCatalogEntry next = list->entries[at];
        unsigned position = at;
        while (position > 0 && compare(next.name, list->entries[position - 1].name) < 0) {
            list->entries[position] = list->entries[position - 1];
            position--;
        }
        list->entries[position] = next;
    }
}
