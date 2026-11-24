#include "language.h"

#include "core/hash.h"
#include "core/utf8.h"
#include "default_theme.h"
#include "gfx/font_pack.h"
#include "hal/audio.h"
#include "hal/system.h"
#include "language_source.h"

enum {
    CHUNK_BYTES = 128,
    MODULE_ALIGN = 4,
    OFFSET_BYTES = 2,
    CHECKED_GLYPHS = 64
};

typedef struct LanguageFontCheck {
    const Font *faces[2 * TEXT_SIZE_COUNT + 1];
    uint16_t checked[CHECKED_GLYPHS];
} LanguageFontCheck;

static const char SUFFIX[] = ".pack";
static const uint16_t FIRSTS[STRING_SEGMENT_COUNT + 1] = STRING_SEGMENT_FIRSTS;

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

static void list_put(LanguageList *list, const I18nPackInfo *info, bool needs_fonts)
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
    list->needs_extra[index] = needs_fonts;
}

static bool extra_font_covers(const char *code)
{
    for (uint32_t index = 0; font_pack_extra_language(index) != NULL; index++) {
        if (same_text(font_pack_extra_language(index), code)) {
            return true;
        }
    }
    return false;
}

bool language_list_needs_fonts(const LanguageList *list, int index)
{
    return list->needs_extra[index] && !extra_font_covers(list->codes[index]);
}

static bool glyph_drawable(LanguageFontCheck *fonts, uint32_t codepoint)
{
    if (codepoint < 0xA0) {
        return true;
    }
    if (codepoint > UINT16_MAX) {
        return false;
    }
    uint16_t *cached = &fonts->checked[codepoint % CHECKED_GLYPHS];
    if (*cached == codepoint) {
        return true;
    }
    hal_audio_service();
    for (uint32_t face = 0; face < sizeof fonts->faces / sizeof fonts->faces[0]; face++) {
        const Glyph *glyph = font_find_glyph(fonts->faces[face], codepoint);
        if (glyph == NULL || glyph->codepoint != codepoint) {
            return false;
        }
    }
    *cached = (uint16_t)codepoint;
    return true;
}

static bool chunk_drawable(LanguageFontCheck *fonts, const char *text, size_t size)
{
    const char *end = text + size;
    while (text < end) {
        if (*text == '\0') {
            text++;
        } else if (!glyph_drawable(fonts, utf8_next(&text))) {
            return false;
        }
    }
    return true;
}

static bool segment_drawable(LanguageFontCheck *fonts, const LanguageSource *source,
                             uint32_t offset, uint32_t size)
{
    char chunk[CHUNK_BYTES];
    uint32_t done = 0;
    while (done < size) {
        hal_system_keep_alive();
        uint32_t take = size - done < sizeof chunk ? size - done : sizeof chunk;
        bool read = source->from_file ? hal_file_read(source->file, offset + done, chunk, take)
                                      : hal_language_read(source->pack, offset + done, chunk, take);
        size_t whole = read ? utf8_complete_length(chunk, take) : 0;
        if (whole == 0 || (done + take == size && whole != take) ||
            !chunk_drawable(fonts, chunk, whole)) {
            return false;
        }
        done += (uint32_t)whole;
    }
    return true;
}

static bool pack_needs_fonts(LanguageFontCheck *fonts, const LanguageSource *source,
                             const I18nPackInfo *info, const uint8_t *index)
{
    if ((info->flags & I18N_NEEDS_EXTRA_FONT) == 0 || extra_font_covers(info->code)) {
        return false;
    }
    if (fonts == NULL || !chunk_drawable(fonts, info->name, sizeof info->name)) {
        return true;
    }
    for (uint32_t segment = 0; segment < STRING_SEGMENT_COUNT; segment++) {
        I18nSegment row;
        uint32_t table = OFFSET_BYTES * (uint32_t)(FIRSTS[segment + 1] - FIRSTS[segment]);
        if (!i18n_pack_segment(index, segment, &row) || row.offset > info->size ||
            row.size > info->size - row.offset || row.size <= table ||
            !segment_drawable(fonts, source, row.offset + table, row.size - table)) {
            return true;
        }
    }
    return false;
}

static void list_built_in(LanguageList *list, LanguageFontCheck *fonts)
{
    for (uint8_t copy = 0; copy < HAL_LANGUAGE_COPIES && list->count == 0; copy++) {
        for (uint8_t index = 0; index < LANGUAGE_CAPACITY; index++) {
            LanguageSource source = {.pack = {.copy = copy, .index = index}};
            I18nPackInfo info;
            uint8_t header[I18N_INDEX_BYTES];
            if (!hal_language_read(source.pack, 0, header, sizeof header) ||
                !i18n_pack_info(header, sizeof header, &info)) {
                break;
            }
            list_put(list, &info, pack_needs_fonts(fonts, &source, &info, header));
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

static bool file_sound(HalFile file, const char *code, I18nPackInfo *info, uint8_t *index)
{
    uint32_t capacity = 0;
    hal_language_memory(&capacity);
    if (!hal_file_read(file, 0, index, I18N_INDEX_BYTES) ||
        !i18n_pack_info(index, I18N_INDEX_BYTES, info) || !same_text(info->code, code) ||
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
    uint8_t index[I18N_INDEX_BYTES];
    bool sound = file_sound(source.file, code, &info, index);
    hal_file_close(&source.file);
    return sound;
}

static void list_file(LanguageList *list, const char *name, LanguageFontCheck *fonts)
{
    char code[I18N_CODE_BYTES];
    LanguageSource source;
    I18nPackInfo info;
    if (!code_of_file(name, code) || !language_source_open(code, &source)) {
        return;
    }
    uint8_t index[I18N_INDEX_BYTES];
    if (file_sound(source.file, code, &info, index)) {
        list_put(list, &info, pack_needs_fonts(fonts, &source, &info, index));
    }
    hal_file_close(&source.file);
}

static void list_files(LanguageList *list, LanguageFontCheck *fonts)
{
    HalDir dir;
    HalDirEntry entry;
    if (!hal_dir_open(&dir, LANGUAGE_FOLDER)) {
        return;
    }
    while (hal_dir_next(&dir, &entry)) {
        if (!entry.directory) {
            list_file(list, entry.name, fonts);
        }
    }
    hal_dir_close(&dir);
}

static void build_list(LanguageList *list, LanguageFontCheck *fonts)
{
    list->count = 0;
    list_built_in(list, fonts);
    list_files(list, fonts);
}

void language_list(LanguageList *list)
{
    build_list(list, NULL);
}

void language_list_for_picker(LanguageList *list, const Theme *theme)
{
    LanguageFontCheck fonts = {.faces = {theme->fonts.small}};
    Theme sized = {0};
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        default_theme_set_text_size(&sized, (TextSize)size);
        fonts.faces[2 * size + 1] = sized.fonts.body;
        fonts.faces[2 * size + 2] = sized.fonts.title;
    }
    build_list(list, &fonts);
}
