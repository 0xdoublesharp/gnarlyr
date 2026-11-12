#include "parametric_store.h"

#include <stddef.h>

#include "equalizer_presets.h"
#include "hal/storage.h"

static ParametricBank bank;

ParametricBank *parametric_store_bank(void)
{
    return &bank;
}

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t prefix[PARAMETRIC_FILE_BYTES];
    if (offset > sizeof prefix || length > sizeof prefix - offset ||
        !hal_internal_file_read(PARAMETRIC_STORE_PATH, prefix, offset + length)) {
        return false;
    }
    uint8_t *destination = bytes;
    for (uint32_t index = 0; index < length; index++) {
        destination[index] = prefix[offset + index];
    }
    return true;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    return hal_internal_file_write_at(PARAMETRIC_STORE_PATH, offset, bytes, length);
}

void parametric_store_read(ParametricStore *store, ParametricEq *custom)
{
    bool uncertain = store->uncertain;
    *store = (ParametricStore){.uncertain = uncertain};
    StoreFile file = {
        .source = {read_file, NULL}, .write = write_file, .size = PARAMETRIC_FILE_BYTES};
    bank = (ParametricBank){0};
    parametric_file_open(&store->file, file, &bank);
    if (bank.shaping[0].active) {
        sound_shaping_words(&bank.shaping[0], store->description, sizeof store->description);
    }
    store->file.needs_reload |= uncertain;
    if (custom != NULL) {
        *custom = bank.custom;
    }
}

int parametric_store_slot(const Settings *settings)
{
    EqualizerSlots graphic = {0};
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    if (settings->equalizer == EQUALIZER_CUSTOM &&
        hal_internal_file_read(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes)) {
        equalizer_slots_decode(bytes, sizeof bytes, &graphic);
    }
    for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
        bool parametric = (bank.used >> slot & 1) != 0;
        if (settings->equalizer == EQUALIZER_PARAMETRIC && parametric &&
            parametric_same(&settings->parametric, &bank.slots[slot])) {
            return slot;
        }
        if (settings->equalizer == EQUALIZER_CUSTOM && !parametric &&
            equalizer_slots_used(&graphic, slot)) {
            int band = 0;
            while (band < SETTINGS_EQUALIZER_BANDS &&
                   graphic.gains[slot][band] == settings->equalizer_gains[band]) {
                band++;
            }
            if (band == SETTINGS_EQUALIZER_BANDS) {
                return slot;
            }
        }
    }
    return -1;
}
