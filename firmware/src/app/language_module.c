#include <stddef.h>

#include "language.h"
#include "language_source.h"

static const uint8_t MODULES[STRING_SEGMENT_COUNT] = STRING_SEGMENT_MODULES;

static bool module_read(const LanguageSource *source, uint32_t offset, void *buffer,
                        uint32_t length)
{
    return source->from_file ? hal_file_read(source->file, offset, buffer, length)
                             : hal_language_read(source->pack, offset, buffer, length);
}

static uint32_t load_from(const LanguageSource *source, uint32_t segment)
{
    uint8_t index[I18N_INDEX_BYTES];
    I18nPackInfo info;
    I18nSegment row;
    uint8_t *area = language_state.module_area;
    i18n_detach_segment();
    bool loaded =
        module_read(source, 0, index, sizeof index) && i18n_pack_info(index, sizeof index, &info) &&
        i18n_pack_segment(index, segment, &row) && row.size <= language_state.module_room &&
        module_read(source, row.offset, area, row.size) && i18n_attach_segment(segment, area, &row);
    return loaded ? row.checksum : 0;
}

static uint32_t segment_of(uint32_t module)
{
    for (uint32_t segment = 1; segment < STRING_SEGMENT_COUNT; segment++) {
        if (MODULES[segment] == module) {
            return segment;
        }
    }
    return 0;
}

bool language_use_module(uint32_t module)
{
    uint32_t segment = segment_of(module);
    if (segment == 0 || !i18n_attached() || i18n_segment_loaded() == segment) {
        return false;
    }
    uint32_t checksum = 0;
    for (uint8_t attempt = 0; attempt <= HAL_LANGUAGE_COPIES && checksum == 0; attempt++) {
        LanguageSource source = language_state.current;
        if (attempt > 0) {
            source = (LanguageSource){.pack = {.copy = (uint8_t)(attempt - 1), .index = 0}};
        }
        if (!source.from_file) {
            checksum = load_from(&source, segment);
        } else if (language_source_open(i18n_code(), &source)) {
            checksum = load_from(&source, segment);
            hal_file_close(&source.file);
        }
    }
    bool changed = language_state.shown[segment] != checksum;
    language_state.shown[segment] = checksum;
    return changed;
}
