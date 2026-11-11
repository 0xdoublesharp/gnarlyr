#include "equalizer_presets.h"

#include "core/string_builder.h"
#include "hal/storage.h"
#include "settings/equalizer_fit.h"
#include "settings/parametric_import.h"

enum {
    PATH_CAPACITY = EQUALIZER_PRESET_NAME_CAPACITY + 8,
    CHUNK_BYTES = 256,
    MAX_FILE_BYTES = 16384
};

static const char FOLDER[] = "/EQ";
static const StringId SLOT_LABELS[EQUALIZER_SLOT_COUNT] = {
    STR_EQ_SLOT_1, STR_EQ_SLOT_2, STR_EQ_SLOT_3, STR_EQ_SLOT_4,
    STR_EQ_SLOT_5, STR_EQ_SLOT_6, STR_EQ_SLOT_7, STR_EQ_SLOT_8};

const char *equalizer_presets_slot_label(int slot)
{
    return i18n_text(SLOT_LABELS[slot]);
}

void equalizer_presets_read_slots(EqualizerPresets *presets)
{
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    bool read = hal_internal_file_read(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes);
    if (!read || !equalizer_slots_decode(bytes, sizeof bytes, &presets->slots)) {
        presets->slots = (EqualizerSlots){0};
    }
}

bool equalizer_presets_save_slot(EqualizerPresets *presets, int slot, const int8_t *gains)
{
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_presets_read_slots(presets);
    equalizer_slots_put(&presets->slots, slot, gains);
    equalizer_slots_encode(&presets->slots, bytes);
    if (!hal_internal_file_write(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes)) {
        return false;
    }
    if (presets->parametric != NULL && (parametric_store_bank()->used >> slot & 1) != 0) {
        ParametricBank bank = *parametric_store_bank();
        bank.used &= (uint8_t)~(1u << slot);
        return parametric_store_save(presets->parametric, &bank);
    }
    return true;
}

static bool name_fits(const HalDirEntry *entry)
{
    uint32_t length = 0;
    while (length < EQUALIZER_PRESET_NAME_CAPACITY && entry->name[length] != '\0') {
        length++;
    }
    return length > 0 && length < EQUALIZER_PRESET_NAME_CAPACITY && entry->name[0] != '.';
}

static void add_file(EqualizerPresets *presets, const HalDirEntry *entry)
{
    char *name = presets->files[presets->file_count];
    for (int index = 0; index < EQUALIZER_PRESET_NAME_CAPACITY; index++) {
        name[index] = entry->name[index];
    }
    presets->options[presets->option_count++] = name;
    presets->file_count++;
}

static void scan_files(EqualizerPresets *presets)
{
    HalDir dir;
    HalDirEntry entry;
    if (!hal_dir_open(&dir, FOLDER)) {
        return;
    }
    while (presets->file_count < EQUALIZER_PRESET_FILES && hal_dir_next(&dir, &entry)) {
        if (!entry.directory && !entry.hidden && name_fits(&entry)) {
            add_file(presets, &entry);
        }
    }
    hal_dir_close(&dir);
}

void equalizer_presets_scan(EqualizerPresets *presets)
{
    presets->option_count = 0;
    presets->slot_count = 0;
    presets->file_count = 0;
    equalizer_presets_read_slots(presets);
    for (int index = 0; index < EQUALIZER_PRESET_COUNT; index++) {
        presets->options[presets->option_count++] = i18n_text(EQUALIZER_PRESET_NAMES[index]);
    }
    for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
        if (equalizer_presets_slot_used(presets, slot)) {
            presets->slot_at[presets->slot_count++] = (uint8_t)slot;
            presets->options[presets->option_count++] = equalizer_presets_slot_label(slot);
        }
    }
    scan_files(presets);
}

int equalizer_presets_matching_slot(const EqualizerPresets *presets, const int8_t *gains)
{
    for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
        int band = 0;
        while (band < SETTINGS_EQUALIZER_BANDS && presets->slots.gains[slot][band] == gains[band]) {
            band++;
        }
        if (band == SETTINGS_EQUALIZER_BANDS && equalizer_slots_used(&presets->slots, slot) &&
            !(presets->parametric != NULL && (parametric_store_bank()->used >> slot & 1) != 0)) {
            return slot;
        }
    }
    return -1;
}

int equalizer_presets_slot_option(const EqualizerPresets *presets, int slot)
{
    int option = EQUALIZER_PRESET_COUNT;
    for (int below = 0; below < slot; below++) {
        option += equalizer_presets_slot_used(presets, below);
    }
    return option;
}

int equalizer_presets_option_slot(const EqualizerPresets *presets, int option)
{
    int index = option - EQUALIZER_PRESET_COUNT;
    return index >= 0 && index < presets->slot_count ? presets->slot_at[index] : -1;
}

bool equalizer_presets_load(EqualizerPresets *presets, int option, int8_t *gains)
{
    Settings settings = settings_defaults();
    if (!equalizer_presets_choose(presets, option, &settings) ||
        settings.equalizer != EQUALIZER_CUSTOM) {
        return false;
    }
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        gains[band] = settings.equalizer_gains[band];
    }
    return true;
}

bool equalizer_presets_slot_used(const EqualizerPresets *presets, int slot)
{
    return slot >= 0 && slot < EQUALIZER_SLOT_COUNT &&
           (equalizer_slots_used(&presets->slots, slot) ||
            (presets->parametric != NULL && (parametric_store_bank()->used >> slot & 1) != 0));
}

bool equalizer_presets_choose(EqualizerPresets *presets, int option, Settings *settings)
{
    presets->trimmed = false;
    if (option < EQUALIZER_PRESET_COUNT) {
        settings->equalizer = (EqualizerPreset)option;
        return true;
    }
    int slot = equalizer_presets_option_slot(presets, option);
    if (slot >= 0 && presets->parametric != NULL &&
        (parametric_store_bank()->used >> slot & 1) != 0) {
        settings->parametric = parametric_store_bank()->slots[slot];
        settings->equalizer = EQUALIZER_PARAMETRIC;
        return true;
    }
    int file_index = option - EQUALIZER_PRESET_COUNT - presets->slot_count;
    if (slot < 0 && file_index >= 0 && file_index < presets->file_count) {
        char path[PATH_CAPACITY];
        StringBuilder builder = string_builder_make(path, sizeof path);
        string_builder_append(&builder, FOLDER);
        string_builder_append_char(&builder, '/');
        string_builder_append(&builder, presets->files[file_index]);
        HalFile file;
        if (builder.overflowed || !hal_file_open(&file, path)) {
            return false;
        }
        ParametricImport parser;
        EqualizerFit fit;
        equalizer_fit_start(&fit);
        parametric_import_start(&parser);
        char chunk[CHUNK_BYTES];
        bool valid = file.size <= MAX_FILE_BYTES;
        for (uint32_t offset = 0; valid && offset < file.size; offset += CHUNK_BYTES) {
            uint32_t length = file.size - offset < CHUNK_BYTES ? file.size - offset : CHUNK_BYTES;
            valid = hal_file_read(file, offset, chunk, length);
            if (valid) {
                parametric_import_text(&parser, chunk, length);
                equalizer_fit_text(&fit, chunk, length);
            }
        }
        hal_file_close(&file);
        ParametricEq eq;
        bool parsed = parametric_import_finish(&parser, &eq);
        if (!valid || (parser.has_filters && !parsed)) {
            return false;
        }
        if (parsed) {
            settings->parametric = eq;
            settings->equalizer = EQUALIZER_PARAMETRIC;
            presets->trimmed = parser.total > PARAMETRIC_FILTERS;
            return true;
        }
        if (!equalizer_fit_finish(&fit, settings->equalizer_gains)) {
            return false;
        }
    } else if (slot >= 0) {
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            settings->equalizer_gains[band] = presets->slots.gains[slot][band];
        }
    } else {
        return false;
    }
    settings->equalizer = EQUALIZER_CUSTOM;
    return true;
}
