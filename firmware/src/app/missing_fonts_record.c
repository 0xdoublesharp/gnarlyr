#include "missing_fonts.h"

#include <stddef.h>

#include "core/bytes.h"
#include "core/utf8.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library/library.h"
#include "library_store.h"

enum {
    MAP_BYTES = 0x10000 / 8,
    CHUNK_BYTES = 1024,
    ROW_BYTES = 768,
    SECTION_TABLE = 8,
    SECTION_ENTRY_BYTES = 12,
    MAGIC = 0x42445344,
    WORK_BYTES = 2 * MAP_BYTES + ROW_BYTES + CHUNK_BYTES + 1 + MISSING_FONTS_FILE_BYTES
};

typedef struct MissingScan {
    uint8_t *checked;
    uint8_t *missing;
    uint8_t *rows;
    char *chunk;
    uint8_t *text;
    const Font *font;
    HalFile file;
    uint32_t strings_offset;
    uint32_t strings_size;
    uint32_t cached_start;
    uint32_t cached_size;
    uint32_t names;
    bool found;
} MissingScan;

static bool bit_of(const uint8_t *map, uint32_t codepoint)
{
    return ((map[codepoint >> 3] >> (codepoint & 7)) & 1u) != 0;
}

static void set_bit(uint8_t *map, uint32_t codepoint)
{
    map[codepoint >> 3] = (uint8_t)(map[codepoint >> 3] | (1u << (codepoint & 7)));
}

static bool undrawable(MissingScan *scan, uint32_t codepoint)
{
    if (codepoint > 0xFFFF) {
        return false;
    }
    if (!bit_of(scan->checked, codepoint)) {
        set_bit(scan->checked, codepoint);
        if (missing_fonts_undrawable(scan->font, codepoint)) {
            set_bit(scan->missing, codepoint);
            scan->found = true;
        }
    }
    return bit_of(scan->missing, codepoint);
}

static void check_name(MissingScan *scan, const char *name)
{
    bool affected = false;
    for (uint32_t codepoint = utf8_next(&name); codepoint != 0; codepoint = utf8_next(&name)) {
        affected |= undrawable(scan, codepoint);
    }
    scan->names += affected;
}

static const char *cached_name(const MissingScan *scan, uint32_t offset)
{
    uint32_t start = offset - scan->cached_start;
    for (uint32_t at = start; at < scan->cached_size; at++) {
        if (scan->chunk[at] == '\0') {
            return scan->chunk + start;
        }
    }
    return NULL;
}

static const char *read_name(MissingScan *scan, uint32_t offset)
{
    const char *name = cached_name(scan, offset);
    if (name != NULL) {
        return name;
    }
    if (offset >= scan->strings_size) {
        return NULL;
    }
    uint32_t left = scan->strings_size - offset;
    uint32_t take = left < CHUNK_BYTES ? left : CHUNK_BYTES;
    if (!hal_file_read(scan->file, scan->strings_offset + offset, scan->chunk, take)) {
        return NULL;
    }
    scan->cached_start = offset;
    scan->cached_size = (uint32_t)utf8_complete_length(scan->chunk, take);
    scan->chunk[scan->cached_size] = '\0';
    return scan->chunk;
}

static bool check_rows(MissingScan *scan, uint32_t bytes, uint32_t stride)
{
    for (uint32_t at = 0; at < bytes; at += stride) {
        uint32_t offset = bytes_le32(scan->rows + at);
        if (offset != 0) {
            const char *name = read_name(scan, offset);
            if (name == NULL) {
                return false;
            }
            check_name(scan, name);
        }
    }
    return true;
}

static bool scan_section(MissingScan *scan, const uint8_t *entry, uint32_t stride)
{
    uint32_t offset = bytes_le32(entry);
    uint32_t count = bytes_le32(entry + 4);
    if (bytes_le16(entry + 8) != stride || offset < LIBRARY_HEADER_SIZE ||
        offset > scan->file.size || count > (scan->file.size - offset) / stride) {
        return false;
    }
    for (uint32_t left = count * stride; left > 0;) {
        hal_system_keep_alive();
        uint32_t take = left < ROW_BYTES ? left : ROW_BYTES;
        if (!hal_file_read(scan->file, offset, scan->rows, take) ||
            !check_rows(scan, take, stride)) {
            return false;
        }
        offset += take;
        left -= take;
    }
    return true;
}

static bool scan_library(MissingScan *scan)
{
    uint8_t header[LIBRARY_HEADER_SIZE];
    if (!hal_file_read(scan->file, 0, header, sizeof header) || bytes_le32(header) != MAGIC ||
        bytes_le16(header + 4) != LIBRARY_VERSION ||
        bytes_le16(header + 6) != LIBRARY_SECTION_COUNT) {
        return false;
    }
    const uint8_t *table = header + SECTION_TABLE;
    const uint8_t *strings = table + LIBRARY_STRINGS * SECTION_ENTRY_BYTES;
    scan->strings_offset = bytes_le32(strings);
    scan->strings_size = bytes_le32(strings + 4);
    if (scan->strings_offset < sizeof header || scan->strings_offset > scan->file.size ||
        scan->strings_size > scan->file.size - scan->strings_offset) {
        return false;
    }
    static const uint8_t NAME_STRIDES[] = {32, 16, 12, 12, 12};
    for (uint32_t section = LIBRARY_TRACKS; section <= LIBRARY_GENRES; section++) {
        if (!scan_section(scan, table + section * SECTION_ENTRY_BYTES, NAME_STRIDES[section])) {
            return false;
        }
    }
    return true;
}

static uint32_t encode(uint8_t *out, uint32_t codepoint)
{
    if (codepoint < 0x80) {
        out[0] = (uint8_t)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (uint8_t)(0xC0 | (codepoint >> 6));
        out[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    out[0] = (uint8_t)(0xE0 | (codepoint >> 12));
    out[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
    return 3;
}

static void write_text(const MissingScan *scan)
{
    uint32_t length = 0;
    for (uint32_t codepoint = 0; codepoint <= 0xFFFF; codepoint++) {
        if (bit_of(scan->missing, codepoint) && length + 3 <= MISSING_FONTS_FILE_BYTES) {
            length += encode(scan->text + length, codepoint);
        }
    }
    for (uint32_t index = length; index < MISSING_FONTS_FILE_BYTES; index++) {
        scan->text[index] = 0;
    }
}

static void store(const char *path, const uint8_t *bytes, uint32_t size, bool create)
{
    HalFile file;
    if (create) {
        (void)hal_file_reserve(path, size);
    }
    if (!hal_file_open(&file, path)) {
        return;
    }
    (void)hal_file_write_open(file, 0, bytes, file.size < size ? file.size : size);
    hal_file_close(&file);
}

void missing_fonts_record(void *memory, uint32_t size, const Font *font)
{
    if (memory == NULL || size < WORK_BYTES || font == NULL) {
        return;
    }
    uint8_t *work = memory;
    for (uint32_t index = 0; index < 2 * MAP_BYTES; index++) {
        work[index] = 0;
    }
    MissingScan scan = {.checked = work,
                        .missing = work + MAP_BYTES,
                        .rows = work + 2 * MAP_BYTES,
                        .chunk = (char *)work + 2 * MAP_BYTES + ROW_BYTES,
                        .text = work + 2 * MAP_BYTES + ROW_BYTES + CHUNK_BYTES + 1,
                        .font = font};
    if (!hal_file_open(&scan.file, LIBRARY_STORE_PATH)) {
        return;
    }
    bool sound = scan_library(&scan);
    hal_file_close(&scan.file);
    if (!sound) {
        return;
    }
    write_text(&scan);
    uint8_t count[MISSING_FONTS_COUNT_BYTES];
    bytes_put_le32(count, scan.names);
    store(MISSING_FONTS_PATH, scan.text, MISSING_FONTS_FILE_BYTES, scan.found);
    store(MISSING_FONTS_COUNT_PATH, count, sizeof count, scan.found);
}
