#include "missing_fonts.h"

#include <stddef.h>

#include "core/bytes.h"
#include "core/utf8.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library_store.h"

enum {
    MAP_BYTES = 0x10000 / 8,
    CHUNK_BYTES = 1024,
    LIBRARY_HEADER = 128,
    STRINGS_ENTRY = 8 + 9 * 12,
    MAGIC = 0x42445344,
    VERSION = 2,
    WORK_BYTES = 2 * MAP_BYTES + CHUNK_BYTES + 1 + MISSING_FONTS_FILE_BYTES
};

typedef struct MissingScan {
    uint8_t *checked;
    uint8_t *missing;
    char *chunk;
    uint8_t *text;
    const Font *font;
    uint32_t names;
    uint32_t characters;
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
            scan->characters++;
        }
    }
    return bit_of(scan->missing, codepoint);
}

static void check_name(MissingScan *scan, const char *name)
{
    bool affected = false;
    bool path = false;
    for (uint32_t codepoint = utf8_next(&name); codepoint != 0; codepoint = utf8_next(&name)) {
        path |= codepoint == '/';
        affected |= undrawable(scan, codepoint);
    }
    scan->names += affected && !path ? 1u : 0u;
}

static uint32_t check_names(MissingScan *scan, uint32_t length)
{
    uint32_t start = 0;
    for (uint32_t at = 0; at < length; at++) {
        if (scan->chunk[at] == '\0') {
            check_name(scan, scan->chunk + start);
            start = at + 1;
        }
    }
    return start;
}

static void read_names(MissingScan *scan, HalFile file, uint32_t offset, uint32_t length)
{
    uint32_t carry = 0;
    for (uint32_t done = 0; done < length;) {
        hal_system_keep_alive();
        uint32_t take = length - done < CHUNK_BYTES - carry ? length - done : CHUNK_BYTES - carry;
        if (!hal_file_read(file, offset + done, scan->chunk + carry, take)) {
            return;
        }
        done += take;
        uint32_t used = check_names(scan, carry + take);
        carry = carry + take - used;
        for (uint32_t index = 0; index < carry; index++) {
            scan->chunk[index] = scan->chunk[used + index];
        }
        carry = carry == CHUNK_BYTES ? 0 : carry;
    }
}

static bool scan_library(MissingScan *scan)
{
    uint8_t header[LIBRARY_HEADER];
    HalFile file;
    if (!hal_file_open(&file, LIBRARY_STORE_PATH)) {
        return false;
    }
    bool sound = hal_file_read(file, 0, header, sizeof header) && bytes_le32(header) == MAGIC &&
                 bytes_le16(header + 4) == VERSION;
    uint32_t offset = bytes_le32(header + STRINGS_ENTRY);
    uint32_t length = bytes_le32(header + STRINGS_ENTRY + 4);
    sound = sound && offset >= LIBRARY_HEADER && offset <= file.size && length <= file.size - offset;
    if (sound) {
        read_names(scan, file, offset, length);
    }
    hal_file_close(&file);
    return sound;
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
    MissingScan scan = {work, work + MAP_BYTES, (char *)work + 2 * MAP_BYTES,
                        work + 2 * MAP_BYTES + CHUNK_BYTES + 1, font, 0, 0};
    if (!scan_library(&scan)) {
        return;
    }
    write_text(&scan);
    uint8_t count[MISSING_FONTS_COUNT_BYTES];
    bytes_put_le32(count, scan.names);
    store(MISSING_FONTS_PATH, scan.text, MISSING_FONTS_FILE_BYTES, scan.characters > 0);
    store(MISSING_FONTS_COUNT_PATH, count, sizeof count, scan.characters > 0);
}
