#include "gfx/font_pack.h"
#include "core/bytes.h"
#include "core/hash.h"

enum {
    FACE_UNKNOWN = 0,
    FACE_READY = 1,
    FACE_MISSING = 2,
    EXTRA_COUNT_FIELD = 5,
    EXTRA_LANGUAGES_FIELD = 6,
    EXTRA_SIZE_FIELD = 8
};

static const uint8_t MAGIC[4] = {'E', 'G', 'L', 'F'};
static const uint8_t EXTRA_MAGIC[4] = {'E', 'G', 'X', 'F'};

typedef struct PackFace {
    uint8_t state;
    uint8_t roots;
    uint16_t first;
    uint16_t last;
    uint8_t root[2 * FONT_PACK_MAX_ROOT];
    uint32_t count;
    uint32_t size;
    uint32_t seed;
} PackFace;

typedef struct PackSlot {
    uint32_t key;
    bool present;
    Glyph glyph;
    uint8_t bitmap[FONT_PACK_MAX_BITMAP];
} PackSlot;

_Static_assert(offsetof(PackSlot, bitmap) == offsetof(PackSlot, glyph) + sizeof(Glyph),
               "a bitmap follows its glyph");

typedef struct PackState {
    PackFace faces[FONT_PACK_FACES];
    uint8_t sector[FONT_PACK_SECTOR_BYTES];
} PackState;

typedef struct ExtraSource {
    FontPackReader reader;
    uint32_t start[FONT_PACK_EXTRA_FACES];
    uint32_t size[FONT_PACK_EXTRA_FACES];
    uint32_t language_count;
    char languages[FONT_PACK_EXTRA_LANGUAGES * FONT_PACK_LANGUAGE_BYTES];
} ExtraSource;

static FontPackReader pack_reader;
static ExtraSource extra;
static PackState *state;
static PackSlot *slots;
static uint32_t slot_count;
static uint32_t victim;
static FontPackHold hold;
static uint32_t held_bytes;

uint32_t font_pack_attach(FontPackReader reader, void *memory, size_t bytes)
{
    font_pack_detach();
    size_t set_bytes = FONT_PACK_WAYS * sizeof(PackSlot);
    if (reader.read == NULL || memory == NULL || bytes < sizeof(PackState) + set_bytes) {
        return 0;
    }
    uint32_t sets = 1;
    while ((size_t)sets * 2 * set_bytes <= bytes - sizeof(PackState)) {
        sets *= 2;
    }
    pack_reader = reader;
    state = memory;
    slots = (PackSlot *)(state + 1);
    slot_count = sets * FONT_PACK_WAYS;
    font_pack_hold(FONT_PACK_HOLD_NONE);
    return slot_count;
}

void font_pack_detach(void)
{
    state = NULL;
    slots = NULL;
    slot_count = 0;
}

void font_pack_forget(void)
{
    held_bytes = 0;
    if (state == NULL) {
        return;
    }
    for (uint32_t face = 0; face < FONT_PACK_FACES; face++) {
        state->faces[face].state = FACE_UNKNOWN;
    }
    for (uint32_t index = 0; index < slot_count; index++) {
        slots[index].key = 0;
    }
}

void font_pack_hold(FontPackHold next)
{
    if (next != FONT_PACK_HOLD_FREEZE) {
        font_pack_forget();
    }
    hold = next;
}

uint32_t font_pack_held_bytes(void)
{
    return held_bytes;
}

static bool row_fits(uint32_t start, uint32_t size, uint32_t file_size)
{
    return size == 0 || (start % FONT_PACK_SECTOR_BYTES == 0 && start >= FONT_PACK_SECTOR_BYTES &&
                         start <= file_size && size <= file_size - start);
}

static bool extra_header_valid(const uint8_t *header, uint32_t file_size)
{
    for (int byte = 0; byte < 4; byte++) {
        if (header[byte] != EXTRA_MAGIC[byte]) {
            return false;
        }
    }
    bool valid = header[4] == FONT_PACK_VERSION &&
                 header[EXTRA_COUNT_FIELD] == FONT_PACK_EXTRA_FACES &&
                 header[EXTRA_LANGUAGES_FIELD] <= FONT_PACK_EXTRA_LANGUAGES &&
                 bytes_le32(header + EXTRA_SIZE_FIELD) == file_size &&
                 hash_update(HASH_START, header, FONT_PACK_SECTOR_CHECK) ==
                     bytes_le32(header + FONT_PACK_SECTOR_CHECK);
    for (uint32_t face = 0; valid && face < FONT_PACK_EXTRA_FACES; face++) {
        const uint8_t *row = header + FONT_PACK_EXTRA_TABLE + 8 * face;
        valid = row_fits(bytes_le32(row), bytes_le32(row + 4), file_size);
    }
    return valid;
}

static void keep_extra_header(const uint8_t *header)
{
    for (uint32_t face = 0; face < FONT_PACK_EXTRA_FACES; face++) {
        extra.start[face] = bytes_le32(header + FONT_PACK_EXTRA_TABLE + 8 * face);
        extra.size[face] = bytes_le32(header + FONT_PACK_EXTRA_TABLE + 8 * face + 4);
    }
    extra.language_count = header[EXTRA_LANGUAGES_FIELD];
    for (uint32_t byte = 0; byte < sizeof extra.languages; byte++) {
        bool last = byte % FONT_PACK_LANGUAGE_BYTES == FONT_PACK_LANGUAGE_BYTES - 1;
        extra.languages[byte] = last ? '\0' : (char)header[FONT_PACK_EXTRA_CODES + byte];
    }
}

bool font_pack_attach_extra(FontPackReader reader, uint32_t size)
{
    font_pack_detach_extra();
    if (state == NULL || reader.read == NULL ||
        !reader.read(reader.context, FONT_PACK_IMAGE_FACES, 0, state->sector,
                     FONT_PACK_SECTOR_BYTES) ||
        !extra_header_valid(state->sector, size)) {
        return false;
    }
    keep_extra_header(state->sector);
    extra.reader = reader;
    font_pack_hold(FONT_PACK_HOLD_NONE);
    return true;
}

void font_pack_detach_extra(void)
{
    extra.reader.read = NULL;
    extra.language_count = 0;
}

bool font_pack_extra_attached(void)
{
    return extra.reader.read != NULL;
}

const char *font_pack_extra_language(uint32_t index)
{
    return index < extra.language_count ? extra.languages + index * FONT_PACK_LANGUAGE_BYTES : NULL;
}

static bool read_extra(uint32_t face, uint32_t offset, void *buffer, uint32_t length)
{
    uint32_t index = face - FONT_PACK_IMAGE_FACES;
    if (extra.reader.read == NULL || hold == FONT_PACK_HOLD_FREEZE || offset > extra.size[index] ||
        length > extra.size[index] - offset) {
        return false;
    }
    return extra.reader.read(extra.reader.context, face, extra.start[index] + offset, buffer,
                             length);
}

static bool read_face(uint32_t face, uint32_t offset, void *buffer, uint32_t length)
{
    if (face >= FONT_PACK_IMAGE_FACES) {
        return read_extra(face, offset, buffer, length);
    }
    return pack_reader.read(pack_reader.context, face, offset, buffer, length);
}

static uint32_t sectors_for(uint32_t items, uint32_t per_sector)
{
    return (items + per_sector - 1) / per_sector;
}

static uint32_t index_sectors(const PackFace *face)
{
    return sectors_for(face->count, FONT_PACK_ENTRIES_PER_SECTOR);
}

static bool header_valid(const uint8_t *header)
{
    uint32_t count = bytes_le32(header + 8);
    uint32_t roots =
        sectors_for(sectors_for(count, FONT_PACK_ENTRIES_PER_SECTOR), FONT_PACK_DIRECTORY_ENTRIES);
    for (int byte = 0; byte < 4; byte++) {
        if (header[byte] != MAGIC[byte]) {
            return false;
        }
    }
    return header[4] == FONT_PACK_VERSION && header[5] == FONT_PACK_ENTRY_BYTES &&
           bytes_le16(header + 6) == FONT_PACK_ENTRIES_PER_SECTOR &&
           count - 1u < FONT_PACK_MAX_GLYPHS && bytes_le16(header + 20) == roots &&
           roots <= FONT_PACK_MAX_ROOT && bytes_le16(header + 16) <= bytes_le16(header + 18) &&
           hash_update(HASH_START, header, FONT_PACK_HEADER_CHECK) ==
               bytes_le32(header + FONT_PACK_HEADER_CHECK);
}

static uint32_t data_offset(const PackFace *face)
{
    return (1u + face->roots + index_sectors(face)) * FONT_PACK_SECTOR_BYTES;
}

static bool open_face(uint32_t index, PackFace *face)
{
    const uint8_t *header = state->sector;
    if (!read_face(index, 0, state->sector, FONT_PACK_HEADER_BYTES) || !header_valid(header)) {
        return false;
    }
    face->count = bytes_le32(header + 8);
    face->size = bytes_le32(header + 12);
    face->first = (uint16_t)bytes_le16(header + 16);
    face->last = (uint16_t)bytes_le16(header + 18);
    face->roots = (uint8_t)bytes_le16(header + 20);
    for (uint32_t byte = 0; byte < sizeof face->root; byte++) {
        face->root[byte] = header[FONT_PACK_ROOT_OFFSET + byte];
    }
    face->seed = bytes_le32(header + FONT_PACK_HEADER_CHECK);
    return data_offset(face) <= face->size;
}

static PackFace *ready_face(uint32_t index)
{
    PackFace *face = &state->faces[index];
    if (face->state == FACE_UNKNOWN) {
        face->state = open_face(index, face) ? FACE_READY : FACE_MISSING;
    }
    return face->state == FACE_READY ? face : NULL;
}

static bool read_sector(uint32_t index, const PackFace *face, uint32_t number)
{
    return read_face(index, number * FONT_PACK_SECTOR_BYTES, state->sector,
                     FONT_PACK_SECTOR_BYTES) &&
           hash_update(face->seed, state->sector, FONT_PACK_SECTOR_CHECK) ==
               bytes_le32(state->sector + FONT_PACK_SECTOR_CHECK);
}

static int32_t last_at_or_below(const uint8_t *base, uint32_t count, uint32_t stride,
                                uint32_t codepoint)
{
    int32_t low = 0;
    int32_t high = (int32_t)count - 1;
    int32_t found = -1;
    while (low <= high) {
        int32_t middle = low + (high - low) / 2;
        if (bytes_le16(base + (uint32_t)middle * stride) <= codepoint) {
            found = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return found;
}

static uint32_t smaller(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static int32_t find_index_sector(uint32_t index, const PackFace *face, uint32_t codepoint)
{
    int32_t directory = last_at_or_below(face->root, face->roots, 2, codepoint);
    if (directory < 0 || !read_sector(index, face, 1u + (uint32_t)directory)) {
        return -1;
    }
    uint32_t first = (uint32_t)directory * FONT_PACK_DIRECTORY_ENTRIES;
    uint32_t count = smaller(FONT_PACK_DIRECTORY_ENTRIES, index_sectors(face) - first);
    int32_t within = last_at_or_below(state->sector, count, 2, codepoint);
    return within < 0 ? -1 : (int32_t)(first + (uint32_t)within);
}

static const uint8_t *find_entry(uint32_t index, const PackFace *face, uint32_t codepoint)
{
    int32_t sector = find_index_sector(index, face, codepoint);
    if (sector < 0 || !read_sector(index, face, 1u + face->roots + (uint32_t)sector)) {
        return NULL;
    }
    uint32_t first = (uint32_t)sector * FONT_PACK_ENTRIES_PER_SECTOR;
    uint32_t count = smaller(FONT_PACK_ENTRIES_PER_SECTOR, face->count - first);
    int32_t position = last_at_or_below(state->sector, count, FONT_PACK_ENTRY_BYTES, codepoint);
    const uint8_t *entry = state->sector + (uint32_t)position * FONT_PACK_ENTRY_BYTES;
    return position >= 0 && bytes_le16(entry) == codepoint ? entry : NULL;
}

static bool bitmap_fits(const PackFace *face, uint32_t offset, uint32_t bytes)
{
    return bytes <= FONT_PACK_MAX_BITMAP && offset >= data_offset(face) && offset <= face->size &&
           bytes <= face->size - offset;
}

static bool load_glyph(uint32_t index, const PackFace *face, uint32_t codepoint, PackSlot *slot)
{
    const uint8_t *entry = find_entry(index, face, codepoint);
    if (entry == NULL) {
        return false;
    }
    uint32_t bytes = ((uint32_t)entry[2] * entry[3] + 1) / 2;
    uint32_t offset = bytes_le32(entry + 8);
    slot->glyph = (Glyph){.codepoint = (uint16_t)codepoint,
                          .bitmap_offset = 0,
                          .width = entry[2],
                          .height = entry[3],
                          .left = (int8_t)entry[4],
                          .top = (int8_t)entry[5],
                          .advance = entry[6]};
    return bitmap_fits(face, offset, bytes) &&
           (bytes == 0 || read_face(index, offset, slot->bitmap, bytes));
}

static uint32_t set_of(uint32_t key)
{
    return (key * 2654435761u >> 16) & (slot_count / FONT_PACK_WAYS - 1);
}

static uint32_t held_entry_bytes(const PackSlot *slot)
{
    uint32_t bitmap = ((uint32_t)slot->glyph.width * slot->glyph.height + 1) / 2;
    return ((uint32_t)offsetof(PackSlot, bitmap) + bitmap + 3u) & ~3u;
}

static const Glyph *find_held(uint32_t face, const PackFace *ready, uint32_t codepoint)
{
    uint32_t key = (face + 1) << 16 | codepoint;
    uint8_t *base = (uint8_t *)slots;
    for (uint32_t at = 0; at < held_bytes; at += held_entry_bytes((PackSlot *)(base + at))) {
        PackSlot *slot = (PackSlot *)(base + at);
        if (slot->key == key) {
            return slot->present ? &slot->glyph : NULL;
        }
    }
    PackSlot *slot = (PackSlot *)(base + held_bytes);
    if (held_bytes + sizeof(PackSlot) > slot_count * sizeof(PackSlot)) {
        return NULL;
    }
    slot->present = load_glyph(face, ready, codepoint, slot);
    if (!slot->present && face >= FONT_PACK_IMAGE_FACES && hold == FONT_PACK_HOLD_FREEZE) {
        return NULL;
    }
    if (!slot->present) {
        slot->glyph = (Glyph){0};
    }
    slot->key = key;
    held_bytes += held_entry_bytes(slot);
    return slot->present ? &slot->glyph : NULL;
}

static const Glyph *find_cached(uint32_t face, const PackFace *ready, uint32_t codepoint)
{
    uint32_t key = (face + 1) << 16 | codepoint;
    PackSlot *set = slots + set_of(key) * FONT_PACK_WAYS;
    for (uint32_t way = 0; way < FONT_PACK_WAYS; way++) {
        if (set[way].key == key) {
            return set[way].present ? &set[way].glyph : NULL;
        }
    }
    PackSlot *slot = &set[victim++ % FONT_PACK_WAYS];
    slot->key = 0;
    slot->present = load_glyph(face, ready, codepoint, slot);
    slot->key = key;
    return slot->present ? &slot->glyph : NULL;
}

const Glyph *font_pack_find(uint32_t face, uint32_t codepoint)
{
    const PackFace *ready = state != NULL && face < FONT_PACK_FACES ? ready_face(face) : NULL;
    if (ready == NULL || codepoint < ready->first || codepoint > ready->last) {
        return NULL;
    }
    if (hold != FONT_PACK_HOLD_NONE) {
        return find_held(face, ready, codepoint);
    }
    return find_cached(face, ready, codepoint);
}

const uint8_t *font_pack_bitmap(const Glyph *glyph)
{
    const uint8_t *address = (const uint8_t *)glyph;
    const uint8_t *start = (const uint8_t *)slots;
    if (slots == NULL || address < start || address >= start + slot_count * sizeof(PackSlot)) {
        return NULL;
    }
    return address + sizeof(Glyph);
}
