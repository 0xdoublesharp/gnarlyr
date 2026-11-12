#include "device/updater.h"

#include <stddef.h>

#include "core/seam.h"
#include "device/image_crc.h"

enum {
    SECTOR_BYTES = 512,
    CHUNK_SECTORS = UPDATER_CHUNK_BYTES / SECTOR_BYTES,
    CHUNK_WORDS = UPDATER_CHUNK_BYTES / 4,
    HEADER_BYTES = 0x200,
    PREFIX_BYTES = 0x7B8,
    CONTAINER_BASE_OFFSET = 0x200,
    CONTAINER_CAPACITY_OFFSET = 0x204,
    TABLE_OFFSET = 0x208,
    TABLE_ROWS = 91,
    ROW_BYTES = 16,
    CODE_OFFSET = 0xD68,
    VECTOR_BYTES = 8,
    MINIMUM_DATA = 8,
    MAXIMUM_DATA = 180,
    FW_SIZE_OFFSET = 0x1F4,
    MAGIC_OFFSET = 0x1F8,
    FW1_END_OFFSET = 0x80,
    FW_SIZE = 0x44C000,
    FW_END = 0x500000,
    CHUNK_COUNT = FW_END / UPDATER_CHUNK_BYTES,
    BRAND_OFFSET = 0x10,
    BRAND_BYTES = 8,
    ECHO_STOCK_FW_SIZE = 0x025CC000,
    CRC_BYTES = 4,
    MARKER_LENGTH = 11,
    MARKER_CARRY = MARKER_LENGTH - 1
};

#define CODE_BASE 0x03050000u
#define CODE_TARGET 0x03050B68u
#define CODE_LIMIT 0x030771D4u
#define DATA_BASE 0x03004000u

static const uint8_t MAGIC[8] = {'R', 'K', 'n', 'a', 'n', 'o', 'F', 'W'};
static const uint8_t STOCK_BRAND[BRAND_BYTES] = {'R', 'o', 'c', 'k', 'c', 'h', 'i', 'p'};
static const uint8_t EIGHT_GB_MARKER[MARKER_LENGTH] = {'H', 'I', 'F', 'I', 'E', 'C',
                                                       '0', '0', 'I', 'M', 'G'};
static const uint8_t ECHO_MARKER[MARKER_LENGTH] = {'E', 'C', 'H', 'O', 'V', '1',
                                                   '0', '0', 'I', 'M', 'G'};
static const char OWN_BRAND[BRAND_BYTES + 1] = FIRMWARE_ID;
static const uint8_t SMALL_MODEL_MARKER[MARKER_LENGTH] = {'M', 'I', 'N', 'I', 'V', '1',
                                                          '0', '0', 'I', 'M', 'G'};
static const uint32_t ROW_2[4] = {0x03078CD0u, 0x03079DCCu, 4, 0};

typedef struct ModelScan {
    uint8_t carry[MARKER_CARRY];
    bool carried;
    bool eight_gb;
    bool small_model;
    bool echo;
} ModelScan;

static uint32_t *source_chunk;
static uint32_t *target_chunk;
static uint32_t *prefix;

_Static_assert(2 * CHUNK_WORDS + PREFIX_BYTES / 4 <= UPDATER_SCRATCH_WORDS,
               "scratch holds both chunks and the header");

void updater_use_scratch(uint32_t *words)
{
    source_chunk = words;
    target_chunk = words != NULL ? words + CHUNK_WORDS : NULL;
    prefix = words != NULL ? words + 2 * CHUNK_WORDS : NULL;
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool same_bytes(const uint32_t *left, const uint32_t *right, uint32_t words)
{
    for (uint32_t index = 0; index < words; index++) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool read_file_chunk(const UpdateFile *file, uint32_t chunk, uint32_t *words)
{
    return file->read(file->context, chunk * UPDATER_CHUNK_BYTES, words, UPDATER_CHUNK_BYTES);
}

static bool read_slot_chunk(const UpdateStorage *storage, uint32_t slot, uint32_t chunk,
                            uint32_t *words)
{
    return storage->read(storage->context, slot + chunk * CHUNK_SECTORS, CHUNK_SECTORS, words);
}

static bool has_text(const uint8_t *bytes, const uint8_t *text, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        if (bytes[index] != text[index]) {
            return false;
        }
    }
    return true;
}

static bool header_is_valid(const uint8_t *header, const UpdateStorage *storage)
{
    return has_text(header + MAGIC_OFFSET, MAGIC, sizeof MAGIC) &&
           le32(header + FW_SIZE_OFFSET) == FW_SIZE && le32(header + FW1_END_OFFSET) <= FW_SIZE &&
           FW_END / SECTOR_BYTES <= storage->slot_sectors &&
           le32(header + CONTAINER_BASE_OFFSET) == CODE_BASE &&
           le32(header + CONTAINER_CAPACITY_OFFSET) == TABLE_ROWS;
}

static uint32_t row_word(const uint8_t *header, uint32_t row, uint32_t word)
{
    return le32(header + TABLE_OFFSET + ROW_BYTES * row + 4 * word);
}

static bool code_row_is_valid(const uint8_t *header, uint32_t *code_size)
{
    uint32_t size = row_word(header, 0, 2);
    *code_size = size;
    return row_word(header, 0, 0) == CODE_TARGET && row_word(header, 0, 1) == CODE_BASE &&
           row_word(header, 0, 3) == CODE_TARGET + size && size % 4 == 0 && size >= VECTOR_BYTES &&
           size <= CODE_LIMIT - CODE_TARGET;
}

static bool data_row_is_valid(const uint8_t *header)
{
    uint32_t size = row_word(header, 1, 1);
    return row_word(header, 1, 0) == DATA_BASE && size >= MINIMUM_DATA && size <= MAXIMUM_DATA &&
           size % 4 == 0 && row_word(header, 1, 2) == DATA_BASE + size;
}

static bool other_rows_are_valid(const uint8_t *header)
{
    for (uint32_t word = 0; word < 4; word++) {
        if (row_word(header, 2, word) != ROW_2[word]) {
            return false;
        }
    }
    for (uint32_t row = 3; row < TABLE_ROWS; row++) {
        for (uint32_t word = 0; word < 4; word++) {
            if (row_word(header, row, word) != 0) {
                return false;
            }
        }
    }
    return true;
}

static bool vectors_are_valid(const UpdateFile *file, uint32_t code_size)
{
    uint32_t code[2];
    uint32_t entry[2];
    if (!file->read(file->context, CODE_OFFSET, code, VECTOR_BYTES) ||
        !file->read(file->context, CODE_OFFSET + code_size, entry, VECTOR_BYTES)) {
        return false;
    }
    uint32_t stack = le32((const uint8_t *)&entry[0]);
    uint32_t reset = le32((const uint8_t *)&entry[1]);
    uint32_t handler = reset & ~1u;
    return stack == DATA_BASE && (reset & 1u) != 0 && handler >= CODE_BASE &&
           handler < CODE_BASE + code_size && code[0] == entry[0] && code[1] == entry[1];
}

static bool layout_is_bootable(const UpdateFile *file, const uint8_t *header)
{
    uint32_t code_size = 0;
    return code_row_is_valid(header, &code_size) && data_row_is_valid(header) &&
           other_rows_are_valid(header) && vectors_are_valid(file, code_size);
}

static bool header_copy_matches(const UpdateFile *file, const uint32_t *first_chunk,
                                uint32_t fw_size)
{
    uint32_t copy[HEADER_BYTES / 4];
    return file->read(file->context, fw_size, copy, HEADER_BYTES) &&
           same_bytes(copy, first_chunk, HEADER_BYTES / 4);
}

static bool chunk_matches_slot(const UpdateFile *file, const UpdateStorage *storage, uint32_t slot,
                               uint32_t chunk)
{
    return read_file_chunk(file, chunk, source_chunk) &&
           read_slot_chunk(storage, slot, chunk, target_chunk) &&
           same_bytes(source_chunk, target_chunk, CHUNK_WORDS);
}

static bool matches_installed(const UpdateFile *file, const UpdateStorage *storage,
                              uint32_t fw1_end)
{
    uint32_t used_chunks = (fw1_end + UPDATER_CHUNK_BYTES - 1) / UPDATER_CHUNK_BYTES;
    for (uint32_t chunk = 0; chunk < used_chunks; chunk++) {
        if (!chunk_matches_slot(file, storage, storage->slot_1, chunk)) {
            return false;
        }
    }
    return chunk_matches_slot(file, storage, storage->slot_1, FW_SIZE / UPDATER_CHUNK_BYTES);
}

static void find_markers(ModelScan *scan, const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index + MARKER_LENGTH <= length; index++) {
        if (bytes[index] == EIGHT_GB_MARKER[0] &&
            has_text(bytes + index, EIGHT_GB_MARKER, MARKER_LENGTH)) {
            scan->eight_gb = true;
        } else if (bytes[index] == SMALL_MODEL_MARKER[0] &&
                   has_text(bytes + index, SMALL_MODEL_MARKER, MARKER_LENGTH)) {
            scan->small_model = true;
        } else if (bytes[index] == ECHO_MARKER[0] &&
                   has_text(bytes + index, ECHO_MARKER, MARKER_LENGTH)) {
            scan->echo = true;
        }
    }
}

static void scan_chunk(ModelScan *scan, const uint8_t *bytes)
{
    if (scan->carried) {
        uint8_t seam[2 * MARKER_CARRY];
        for (uint32_t index = 0; index < MARKER_CARRY; index++) {
            seam[index] = scan->carry[index];
            seam[MARKER_CARRY + index] = bytes[index];
        }
        find_markers(scan, seam, sizeof seam);
    }
    find_markers(scan, bytes, UPDATER_CHUNK_BYTES);
    for (uint32_t index = 0; index < MARKER_CARRY; index++) {
        scan->carry[index] = bytes[UPDATER_CHUNK_BYTES - MARKER_CARRY + index];
    }
    scan->carried = true;
}

static bool crc_is_valid(const UpdateFile *file, uint32_t fw_end, ModelScan *scan)
{
    uint32_t crc = 0;
    for (uint32_t chunk = 0; chunk < fw_end / UPDATER_CHUNK_BYTES; chunk++) {
        if (!read_file_chunk(file, chunk, source_chunk)) {
            return false;
        }
        const uint8_t *bytes = (const uint8_t *)source_chunk;
        crc = image_crc_update(crc, bytes, UPDATER_CHUNK_BYTES);
        if (scan != NULL) {
            scan_chunk(scan, bytes);
        }
    }
    uint32_t stored = 0;
    return file->read(file->context, fw_end, &stored, sizeof stored) &&
           crc == le32((const uint8_t *)&stored);
}

static bool stock_fw_size_fits(uint32_t fw_size, uint32_t fw_end)
{
    return fw_size == ECHO_STOCK_FW_SIZE && fw_size >= HEADER_BYTES &&
           fw_size % SECTOR_BYTES == 0 && fw_size <= fw_end - HEADER_BYTES;
}

static bool stock_markers_match(const ModelScan *scan)
{
    return scan->echo && !scan->eight_gb && !scan->small_model;
}

static UpdateCheck stock_check(const UpdateFile *file, const UpdateStorage *storage,
                               const uint32_t *header)
{
    const uint8_t *bytes = (const uint8_t *)header;
    uint32_t fw_size = le32(bytes + FW_SIZE_OFFSET);
    uint32_t fw_end = file->size - CRC_BYTES;
    ModelScan scan = {.carried = false, .eight_gb = false, .small_model = false, .echo = false};
    bool valid = fw_end / SECTOR_BYTES <= storage->slot_sectors &&
                 stock_fw_size_fits(fw_size, fw_end) &&
                 has_text(bytes + MAGIC_OFFSET, MAGIC, sizeof MAGIC) &&
                 has_text(bytes + BRAND_OFFSET, STOCK_BRAND, sizeof STOCK_BRAND) &&
                 header_copy_matches(file, header, fw_size) && crc_is_valid(file, fw_end, &scan);
    return valid && stock_markers_match(&scan) ? UPDATE_STOCK : UPDATE_INVALID;
}

static bool brand_matches(const uint8_t *header)
{
    return has_text(header + BRAND_OFFSET, (const uint8_t *)OWN_BRAND, BRAND_BYTES);
}

CORE_SEAM UpdateCheck updater_check(const UpdateFile *file, const UpdateStorage *storage)
{
    bool stock = file->size == UPDATER_ECHO_STOCK_IMAGE_SIZE;
    if (source_chunk == NULL || (file->size != UPDATER_IMAGE_SIZE && !stock) ||
        !file->read(file->context, 0, prefix, PREFIX_BYTES)) {
        return UPDATE_INVALID;
    }
    if (stock) {
        return stock_check(file, storage, prefix);
    }
    const uint8_t *bytes = (const uint8_t *)prefix;
    if (!brand_matches(bytes) || !header_is_valid(bytes, storage) ||
        !header_copy_matches(file, prefix, FW_SIZE) || !layout_is_bootable(file, bytes)) {
        return UPDATE_INVALID;
    }
    if (matches_installed(file, storage, le32(bytes + FW1_END_OFFSET))) {
        return UPDATE_ALREADY_INSTALLED;
    }
    return crc_is_valid(file, FW_END, NULL) ? UPDATE_VALID : UPDATE_INVALID;
}

bool updater_should_remove_file(UpdateCheck check, bool installed)
{
    return check == UPDATE_VALID || check == UPDATE_STOCK ? installed : true;
}
