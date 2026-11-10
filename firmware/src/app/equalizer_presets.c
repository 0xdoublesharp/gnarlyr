#include "equalizer_presets.h"

#include "core/string_builder.h"
#include "hal/storage.h"
#include "settings/equalizer_fit.h"

enum {
    PATH_CAPACITY = EQUALIZER_PRESET_NAME_CAPACITY + 8,
    CHUNK_BYTES = 256,
    MAX_FILE_BYTES = 16384
};

static const char FOLDER[] = "/EQ";
const StringId EQUALIZER_PRESET_NAMES[EQUALIZER_PRESET_COUNT] = {
    STR_OFF,    STR_EQ_RETRO, STR_EQ_BASS,   STR_EQ_HEAVY,
    STR_EQ_POP, STR_EQ_JAZZ,  STR_EQ_UNIQUE, STR_EQ_CUSTOM};
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
    return hal_internal_file_write(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes);
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
        if (equalizer_slots_used(&presets->slots, slot)) {
            presets->slot_at[presets->slot_count++] = (uint8_t)slot;
            presets->options[presets->option_count++] = equalizer_presets_slot_label(slot);
        }
    }
    scan_files(presets);
}

static bool feed_file(HalFile file, EqualizerFit *fit)
{
    char chunk[CHUNK_BYTES];
    uint32_t size = file.size < MAX_FILE_BYTES ? file.size : MAX_FILE_BYTES;
    for (uint32_t offset = 0; offset < size; offset += CHUNK_BYTES) {
        uint32_t length = size - offset < CHUNK_BYTES ? size - offset : CHUNK_BYTES;
        if (!hal_file_read(file, offset, chunk, length)) {
            return false;
        }
        equalizer_fit_text(fit, chunk, length);
    }
    return true;
}

static bool load_file(const char *name, int8_t *gains)
{
    char path[PATH_CAPACITY];
    StringBuilder builder = string_builder_make(path, sizeof path);
    string_builder_append(&builder, FOLDER);
    string_builder_append_char(&builder, '/');
    string_builder_append(&builder, name);
    HalFile file;
    EqualizerFit fit;
    if (builder.overflowed || !hal_file_open(&file, path)) {
        return false;
    }
    equalizer_fit_start(&fit);
    bool read = feed_file(file, &fit);
    hal_file_close(&file);
    return equalizer_fit_finish(&fit, gains) && read;
}

int equalizer_presets_matching_slot(const EqualizerPresets *presets, const int8_t *gains)
{
    for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
        int band = 0;
        while (band < SETTINGS_EQUALIZER_BANDS && presets->slots.gains[slot][band] == gains[band]) {
            band++;
        }
        if (band == SETTINGS_EQUALIZER_BANDS && equalizer_slots_used(&presets->slots, slot)) {
            return slot;
        }
    }
    return -1;
}

int equalizer_presets_slot_option(const EqualizerPresets *presets, int slot)
{
    int option = EQUALIZER_PRESET_COUNT;
    for (int below = 0; below < slot; below++) {
        option += equalizer_slots_used(&presets->slots, below);
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
    int index = option - EQUALIZER_PRESET_COUNT;
    if (index < 0 || option >= presets->option_count) {
        return false;
    }
    if (index < presets->slot_count) {
        int slot = presets->slot_at[index];
        for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
            gains[band] = presets->slots.gains[slot][band];
        }
        return true;
    }
    return load_file(presets->files[index - presets->slot_count], gains);
}
