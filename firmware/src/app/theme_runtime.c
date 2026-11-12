#include "theme_runtime.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "default_theme.h"
#include "theme_picker.h"

const char THEME_PACK_FOLDER[] = "/" FIRMWARE_DATA_DIRECTORY "/themes";
static const StoreKind KIND = {.magic = {'D', 'S', 'T', 'S'}, .version = 1, .inline_payload = true};

static bool read_selection(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t prefix[1024];
    if (offset > sizeof prefix || length > sizeof prefix - offset ||
        !hal_internal_file_read(THEME_SELECTION_PATH, prefix, offset + length)) {
        return false;
    }
    for (uint32_t at = 0; at < length; at++) {
        ((uint8_t *)bytes)[at] = prefix[offset + at];
    }
    return true;
}

static bool write_selection(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    return hal_internal_file_write_at(THEME_SELECTION_PATH, offset, bytes, length);
}

static StoreFile selection_file(void)
{
    return (StoreFile){.source = {read_selection, NULL}, .write = write_selection, .size = 1024};
}

static StoreLoad check_selection(void *context, uint8_t bank, const StoreHeader *header)
{
    (void)context;
    StoreFile file = selection_file();
    StoreSlot slot = {&file, &KIND, (uint32_t)bank * STORE_BLOCK_BYTES};
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader again;
    StoreLoad load = store_load(&slot, block, &again);
    if (load != STORE_READY) {
        return load;
    }
    return again.generation == header->generation && again.bytes == 17 && again.binding == 0 &&
                   again.count == 0 && block[STORE_FIELD_BYTES + 16] < APPEARANCE_THEME_COUNT
               ? STORE_READY
               : STORE_CORRUPT;
}

static void load_selection(ThemeRuntime *runtime)
{
    StoreFile file = selection_file();
    StorePair pair = {&file, &KIND, STORE_BLOCK_BYTES, check_selection, NULL};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    runtime->blocked = store_blocking(load);
    runtime->stored = load == STORE_READY;
    if (!runtime->stored) {
        return;
    }
    uint8_t block[STORE_BLOCK_BYTES];
    StoreSlot slot = {&file, &KIND, choice.bank * STORE_BLOCK_BYTES};
    StoreHeader header;
    if (store_load(&slot, block, &header) != STORE_READY) {
        runtime->blocked = true;
        return;
    }
    runtime->generation = choice.header.generation;
    runtime->bank = choice.bank;
    runtime->saved_base = block[STORE_FIELD_BYTES + 16];
    for (int at = 0; at < 16; at++) {
        runtime->saved_id[at] = block[STORE_FIELD_BYTES + at];
    }
}

static bool read_pack(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    return hal_file_read(*(HalFile *)context, offset, bytes, length);
}

bool theme_runtime_load(ThemeRuntime *runtime, const char *name)
{
    char path[128];
    StringBuilder builder = string_builder_make(path, sizeof path);
    string_builder_append(&builder, THEME_PACK_FOLDER);
    string_builder_append(&builder, "/");
    string_builder_append(&builder, name);
    HalFile file;
    if (!hal_file_open(&file, path)) {
        return false;
    }
    ThemePack *pack = theme_catalog_work();
    if (!theme_pack_open(pack, (ByteSource){read_pack, &file}, file.size)) {
        hal_file_close(&file);
        return false;
    }
    theme_runtime_close(runtime);
    runtime->pack.palette = pack->palette;
    runtime->pack.base = pack->base;
    runtime->pack.asset_count = pack->asset_count;
    for (int at = 0; at < 16; at++) {
        runtime->pack.id[at] = pack->id[at];
    }
    for (unsigned at = 0; at < pack->asset_count; at++) {
        runtime->pack.assets[at] = pack->assets[at];
    }
    runtime->file = file;
    runtime->active = true;
    return true;
}

void theme_runtime_restore(ThemeRuntime *runtime)
{
    theme_runtime_close(runtime);
    runtime->previewing = false;
    static const uint8_t none[16] = {0};
    runtime->shown_base =
        theme_catalog_same_id(runtime->saved_id, none) ? runtime->saved_base : APPEARANCE_NEON_NOIR;
    ThemeCatalog *catalog = theme_picker_catalog();
    theme_catalog_scan(catalog);
    for (unsigned at = 0; at < catalog->count; at++) {
        if (catalog->entries[at].valid &&
            theme_catalog_same_id(runtime->saved_id, catalog->entries[at].id)) {
            if (theme_runtime_load(runtime, catalog->entries[at].filename)) {
                runtime->shown_base = runtime->pack.base;
            }
            return;
        }
    }
}

void theme_runtime_boot(ThemeRuntime *runtime)
{
    theme_runtime_close(runtime);
    *runtime = (ThemeRuntime){.file = {.handle = -1}, .saved_base = APPEARANCE_NEON_NOIR};
    load_selection(runtime);
    theme_runtime_restore(runtime);
}

bool theme_runtime_save(ThemeRuntime *runtime, int builtin)
{
    if (hal_internal_file_reserve(THEME_SELECTION_PATH, 1024) == HAL_RESERVE_CREATED) {
        runtime->blocked = false;
    }
    if (runtime->blocked) {
        load_selection(runtime);
        if (runtime->blocked) {
            return false;
        }
    }
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    for (int at = 0; at < 16; at++) {
        block[STORE_FIELD_BYTES + at] = builtin < 0 ? runtime->pack.id[at] : 0;
    }
    uint8_t base = builtin < 0 ? runtime->pack.base : (uint8_t)builtin;
    block[STORE_FIELD_BYTES + 16] = base;
    StoreHeader header = {.generation = runtime->stored ? runtime->generation + 1 : 1, .bytes = 17};
    store_encode(&KIND, &header, block);
    uint8_t bank = runtime->stored ? (uint8_t)(1 - runtime->bank) : 0;
    StoreFile file = selection_file();
    StoreSlot slot = {&file, &KIND, bank * STORE_BLOCK_BYTES};
    StoreResult result = store_publish(&slot, block, runtime->stored ? &runtime->generation : NULL);
    runtime->blocked = result == STORE_UNCERTAIN;
    if (result != STORE_DONE) {
        return false;
    }
    runtime->generation = header.generation;
    runtime->bank = bank;
    runtime->stored = true;
    runtime->saved_base = base;
    runtime->shown_base = base;
    runtime->previewing = false;
    for (int at = 0; at < 16; at++) {
        runtime->saved_id[at] = block[STORE_FIELD_BYTES + at];
    }
    return true;
}
