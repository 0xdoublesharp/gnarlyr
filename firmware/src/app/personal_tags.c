#include "personal_tags.h"

#include <stddef.h>

#include "core/utf8.h"
#include "hal/storage.h"

enum {
    CHECKSUM = 1,
    CATALOG,
    CHECK_ROWS,
    PLAN,
    ALLOCATE,
    PREFIX,
    COPY_ROWS,
    COMMIT,
    ROWS_PER_STEP = 4
};

typedef struct PersonalTagRow {
    MixIdentity identity;
    uint32_t mask;
} PersonalTagRow;

static const StoreKind KIND = {.magic = {'D', 'S', 'T', 'G'}, .version = 1};

const char PERSONAL_TAG_PATH_A[] = "/" FIRMWARE_DATA_DIRECTORY "/tags-a.bin";
const char PERSONAL_TAG_PATH_B[] = "/" FIRMWARE_DATA_DIRECTORY "/tags-b.bin";

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const PersonalTagFile *source = context;
    HalFile file;
    if (!hal_file_open(&file, source->path)) {
        return false;
    }
    bool read = hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return read;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const PersonalTagFile *file = context;
    return hal_file_write_at(file->path, offset, bytes, length);
}

static bool find_file(PersonalTagFile *file)
{
    HalFile handle;
    file->size = 0;
    if (!hal_file_open(&handle, file->path)) {
        return false;
    }
    file->size = handle.size;
    hal_file_close(&handle);
    return true;
}

static StoreFile file_io(PersonalTagFile *file)
{
    return (StoreFile){.source = {read_file, file}, .write = write_file, .size = file->size};
}

static bool known_bank(const PersonalTags *tags, uint8_t bank, const StoreFile *file)
{
    uint8_t bytes[STORE_BLOCK_BYTES];
    StoreHeader header;
    StoreSlot slot = {.file = file, .kind = &KIND};
    StoreLoad load = store_load(&slot, bytes, &header);
    if (load == STORE_UNREADABLE || load == STORE_UNSUPPORTED) {
        return false;
    }
    if (load != STORE_READY) {
        return true;
    }
    const StoreHeader *known = &tags->headers[bank];
    return tags->banks[bank] == STORE_READY && header.generation == known->generation &&
           header.bytes == known->bytes && header.hash == known->hash &&
           header.binding == known->binding && header.count == known->count;
}

static bool prepare_file(void *context, uint8_t bank, uint32_t size, StoreFile *io)
{
    PersonalTags *tags = context;
    PersonalTagFile *file = &tags->files[bank];
    bool exists = find_file(file);
    StoreFile previous = file_io(file);
    if (exists && !known_bank(tags, bank, &previous)) {
        return false;
    }
    if (exists && file->size < size) {
        if (!hal_file_remove(file->path)) {
            return false;
        }
        exists = false;
    }
    if (!exists) {
        HalReserveResult reserved = hal_file_reserve(file->path, size);
        if ((reserved != HAL_RESERVE_CREATED && reserved != HAL_RESERVE_EXISTS) ||
            !find_file(file)) {
            return false;
        }
    }
    *io = file_io(file);
    return io->size >= size && known_bank(tags, bank, io);
}

static StoreSlot bank_slot(const PersonalTags *tags, uint8_t bank)
{
    return (StoreSlot){.file = &tags->storage.files[bank], .kind = &KIND};
}

static uint32_t payload_bytes(uint32_t rows)
{
    return PERSONAL_TAG_CATALOG_BYTES + rows * PERSONAL_TAG_ROW_BYTES;
}

static StoreLoad load_header(PersonalTags *tags, uint8_t bank)
{
    if (tags->storage.files[bank].size == 0) {
        return STORE_EMPTY;
    }
    uint8_t bytes[STORE_BLOCK_BYTES];
    StoreSlot slot = bank_slot(tags, bank);
    StoreLoad load = store_load(&slot, bytes, &tags->headers[bank]);
    StoreHeader header = tags->headers[bank];
    bool bounded = header.bytes == payload_bytes(header.count) && header.binding == 0 &&
                   header.bytes <= tags->storage.files[bank].size - STORE_BLOCK_BYTES;
    return load == STORE_READY && !bounded ? STORE_CORRUPT : load;
}

static StoreLoad begin_open(PersonalTags *tags)
{
    tags->banks[0] = load_header(tags, 0);
    tags->banks[1] = load_header(tags, 1);
    for (uint8_t bank = 0; bank < 2; bank++) {
        if (store_blocking(tags->banks[bank])) {
            tags->result = STORE_FAILED;
            return tags->load = tags->banks[bank];
        }
    }
    bool second = tags->banks[1] == STORE_READY &&
                  (tags->banks[0] != STORE_READY ||
                   store_newer(tags->headers[1].generation, tags->headers[0].generation));
    tags->bank = second ? 1 : 0;
    if (tags->banks[tags->bank] == STORE_READY) {
        tags->scan = store_scan_begin();
        tags->stage = CHECKSUM;
        tags->result = STORE_WORKING;
        return tags->load = STORE_EMPTY;
    }
    bool empty = tags->banks[0] == STORE_EMPTY && tags->banks[1] == STORE_EMPTY;
    tags->result = empty ? STORE_IDLE : STORE_FAILED;
    return tags->load = empty ? STORE_EMPTY : STORE_CORRUPT;
}

StoreLoad personal_tags_init(PersonalTags *tags, const Library *library)
{
    *tags = (PersonalTags){.library = library,
                           .files = {{.path = PERSONAL_TAG_PATH_A}, {.path = PERSONAL_TAG_PATH_B}}};
    for (uint8_t bank = 0; bank < 2; bank++) {
        find_file(&tags->files[bank]);
        tags->storage.files[bank] = file_io(&tags->files[bank]);
    }
    tags->storage.prepare = prepare_file;
    tags->storage.context = tags;
    return begin_open(tags);
}

StoreLoad personal_tags_open(PersonalTags *tags, const Library *library, PersonalTagStorage storage)
{
    *tags = (PersonalTags){.library = library, .storage = storage};
    return begin_open(tags);
}

void personal_tags_attach(PersonalTags *tags, const Library *library)
{
    tags->library = library;
}

static bool valid_name(const char *name, char *copy)
{
    char bytes[PERSONAL_TAG_NAME_BYTES + 3] = {0};
    uint32_t length = 0;
    if (name == NULL) {
        return false;
    }
    while (length < PERSONAL_TAG_NAME_BYTES && name[length] != 0) {
        bytes[length] = name[length];
        length++;
    }
    if (length == 0 || length == PERSONAL_TAG_NAME_BYTES) {
        return false;
    }
    const char *cursor = bytes;
    bool named = false;
    while (*cursor != 0) {
        const char *before = cursor;
        uint32_t value = utf8_next(&cursor);
        bool invalid = value == UTF8_REPLACEMENT_CHARACTER && cursor == before + 1;
        if (invalid || value < 32 || (value >= 127 && value <= 159)) {
            return false;
        }
        named |= value != ' ';
    }
    for (uint32_t index = 0; index < PERSONAL_TAG_NAME_BYTES; index++) {
        copy[index] = bytes[index];
    }
    return named;
}

static bool same_name(const char *first, const char *second)
{
    for (uint32_t index = 0; index < PERSONAL_TAG_NAME_BYTES; index++) {
        if (first[index] != second[index]) {
            return false;
        }
        if (first[index] == 0) {
            return true;
        }
    }
    return false;
}

static int identity_compare(const MixIdentity *first, const MixIdentity *second)
{
    for (uint32_t index = 0; index < MIX_PATH_BYTES; index++) {
        uint8_t a = (uint8_t)first->path[index];
        uint8_t b = (uint8_t)second->path[index];
        if (a != b) {
            return a < b ? -1 : 1;
        }
        if (a == 0) {
            break;
        }
    }
    if (first->start_ms != second->start_ms) {
        return first->start_ms < second->start_ms ? -1 : 1;
    }
    return first->end_ms == second->end_ms ? 0 : first->end_ms < second->end_ms ? -1 : 1;
}

static int key_compare(const MixIdentity *first, const MixIdentity *second)
{
    uint32_t a = mix_identity_hash(first);
    uint32_t b = mix_identity_hash(second);
    return a == b ? identity_compare(first, second) : a < b ? -1 : 1;
}

static uint32_t row_offset(uint32_t row)
{
    return STORE_BLOCK_BYTES + PERSONAL_TAG_CATALOG_BYTES + row * PERSONAL_TAG_ROW_BYTES;
}

static StoreLoad read_row(const PersonalTags *tags, uint8_t bank, uint32_t row,
                          PersonalTagRow *record)
{
    uint8_t bytes[PERSONAL_TAG_ROW_BYTES];
    if (!store_file_read(&tags->storage.files[bank], row_offset(row), bytes, sizeof bytes)) {
        return STORE_UNREADABLE;
    }
    record->mask = store_get32(bytes + 4);
    bool valid = mix_identity_decode(bytes + 8, MIX_RECORD_MAX, &record->identity) != 0 &&
                 mix_identity_hash(&record->identity) == store_get32(bytes) && record->mask != 0;
    return valid ? STORE_READY : STORE_CORRUPT;
}

static bool locate(const PersonalTags *tags, const MixIdentity *wanted, uint32_t *row,
                   uint32_t *mask)
{
    uint32_t low = 0;
    uint32_t high = tags->rows;
    *mask = 0;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        PersonalTagRow found;
        if (read_row(tags, tags->active_bank, middle, &found) != STORE_READY ||
            (found.mask & ~tags->catalog.active) != 0) {
            return false;
        }
        int order = key_compare(wanted, &found.identity);
        if (order == 0) {
            *row = middle;
            *mask = found.mask;
            return true;
        }
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    *row = low;
    return true;
}

bool personal_tags_mask(const PersonalTags *tags, uint16_t track, uint32_t *mask)
{
    *mask = 0;
    if ((tags->load != STORE_EMPTY && tags->load != STORE_READY) ||
        (tags->result == STORE_WORKING && tags->stage < PLAN) || tags->library == NULL) {
        return false;
    }
    MixIdentity identity;
    uint32_t row;
    return mix_identity_read(tags->library, track, &identity) == STORE_READY &&
           locate(tags, &identity, &row, mask);
}

bool personal_tags_matches(const PersonalTags *tags, uint16_t track, uint32_t wanted, bool *matches)
{
    uint32_t mask;
    bool read = personal_tags_mask(tags, track, &mask);
    *matches = read && (mask & wanted) == wanted;
    return read;
}

const char *personal_tags_name(const PersonalTags *tags, uint8_t tag)
{
    return tag < PERSONAL_TAG_LIMIT && (tags->catalog.active & (UINT32_C(1) << tag)) != 0
               ? tags->catalog.names[tag]
               : NULL;
}

static bool rename_label(PersonalTags *tags, PersonalTagChange change)
{
    if (!valid_name(change.name, tags->pending.names[change.tag])) {
        return false;
    }
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        if (tag != change.tag && (tags->pending.active & (UINT32_C(1) << tag)) != 0 &&
            same_name(tags->pending.names[tag], tags->pending.names[change.tag])) {
            return false;
        }
    }
    tags->pending.active |= UINT32_C(1) << change.tag;
    return true;
}

static bool edit_label(PersonalTags *tags, PersonalTagChange change)
{
    if (change.tag >= PERSONAL_TAG_LIMIT) {
        return false;
    }
    bool active = (tags->catalog.active & (UINT32_C(1) << change.tag)) != 0;
    if ((change.operation == PERSONAL_TAG_CREATE) == active) {
        return false;
    }
    if (change.operation != PERSONAL_TAG_DELETE) {
        return rename_label(tags, change);
    }
    tags->pending.active &= ~(UINT32_C(1) << change.tag);
    for (uint32_t index = 0; index < PERSONAL_TAG_NAME_BYTES; index++) {
        tags->pending.names[change.tag][index] = 0;
    }
    return true;
}

bool personal_tags_change(PersonalTags *tags, PersonalTagChange change)
{
    if (tags->result == STORE_WORKING || (uint32_t)change.operation > PERSONAL_TAG_ASSIGN ||
        (tags->load != STORE_READY && tags->load != STORE_EMPTY) || tags->storage.prepare == NULL) {
        return false;
    }
    tags->pending = tags->catalog;
    tags->assigning = change.operation == PERSONAL_TAG_ASSIGN;
    tags->target_found = false;
    if (tags->assigning) {
        uint32_t old;
        if ((change.mask & ~tags->catalog.active) != 0 || tags->library == NULL ||
            mix_identity_read(tags->library, change.track, &tags->identity) != STORE_READY ||
            !locate(tags, &tags->identity, &tags->target, &old)) {
            return false;
        }
        tags->target_found = old != 0;
        tags->mask = change.mask;
    } else if (!edit_label(tags, change)) {
        return false;
    }
    tags->source_row = 0;
    tags->output_row = tags->assigning && tags->mask != 0 ? 1 : 0;
    tags->bank = tags->has_active ? (uint8_t)(1 - tags->active_bank) : 0;
    tags->stage = PLAN;
    tags->result = STORE_WORKING;
    return true;
}

static StoreResult failed(PersonalTags *tags)
{
    tags->stage = 0;
    return tags->result = STORE_FAILED;
}

static StoreResult damaged(PersonalTags *tags, StoreLoad load)
{
    tags->banks[tags->bank] = load;
    uint8_t other = (uint8_t)(1 - tags->bank);
    if (!store_blocking(load) && !tags->fallback && tags->banks[other] == STORE_READY) {
        tags->bank = other;
        tags->fallback = true;
        tags->scan = store_scan_begin();
        tags->stage = CHECKSUM;
        return STORE_WORKING;
    }
    tags->load = load;
    return failed(tags);
}

static StoreResult checksum(PersonalTags *tags)
{
    StoreSlot slot = bank_slot(tags, tags->bank);
    uint8_t block[STORE_BLOCK_BYTES];
    StoreHeader header = tags->headers[tags->bank];
    if (!store_scan_next(&slot, &tags->scan, header.bytes, block)) {
        return damaged(tags, STORE_UNREADABLE);
    }
    if (tags->scan.position == header.bytes) {
        if (tags->scan.hash != header.hash) {
            return damaged(tags, STORE_CORRUPT);
        }
        tags->pending = (PersonalTagCatalog){0};
        tags->position = 0;
        tags->stage = CATALOG;
    }
    return STORE_WORKING;
}

static bool catalog_valid(const PersonalTagCatalog *catalog)
{
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        char name[PERSONAL_TAG_NAME_BYTES];
        if ((catalog->active & (UINT32_C(1) << tag)) != 0 &&
            !valid_name(catalog->names[tag], name)) {
            return false;
        }
    }
    return true;
}

static StoreResult read_catalog(PersonalTags *tags)
{
    uint8_t bytes[STORE_BLOCK_BYTES];
    uint32_t left = PERSONAL_TAG_CATALOG_BYTES - tags->position;
    uint32_t count = left < sizeof bytes ? left : sizeof bytes;
    if (!store_file_read(&tags->storage.files[tags->bank], STORE_BLOCK_BYTES + tags->position,
                         bytes, count)) {
        return damaged(tags, STORE_UNREADABLE);
    }
    for (uint32_t index = 0; index < count; index++) {
        uint32_t position = tags->position + index;
        if (position == 0) {
            tags->pending.active = store_get32(bytes);
        }
        if (position >= 4) {
            uint32_t name = position - 4;
            tags->pending.names[name / PERSONAL_TAG_NAME_BYTES][name % PERSONAL_TAG_NAME_BYTES] =
                (char)bytes[index];
        }
    }
    tags->position += count;
    if (tags->position == PERSONAL_TAG_CATALOG_BYTES) {
        if (!catalog_valid(&tags->pending)) {
            return damaged(tags, STORE_CORRUPT);
        }
        tags->source_row = 0;
        tags->stage = CHECK_ROWS;
    }
    return STORE_WORKING;
}

static StoreResult accept_bank(PersonalTags *tags)
{
    tags->catalog = tags->pending;
    tags->active_bank = tags->bank;
    tags->has_active = true;
    tags->banks[tags->bank] = STORE_READY;
    tags->rows = tags->headers[tags->bank].count;
    tags->load = STORE_READY;
    tags->stage = 0;
    return tags->result = STORE_DONE;
}

static StoreResult check_rows(PersonalTags *tags)
{
    uint32_t count = tags->headers[tags->bank].count;
    for (int step = 0; step < ROWS_PER_STEP && tags->source_row < count; step++) {
        PersonalTagRow row;
        StoreLoad load = read_row(tags, tags->bank, tags->source_row, &row);
        if (load != STORE_READY) {
            return damaged(tags, load);
        }
        if ((row.mask & ~tags->pending.active) != 0 ||
            (tags->source_row != 0 && key_compare(&tags->previous, &row.identity) >= 0)) {
            return damaged(tags, STORE_CORRUPT);
        }
        tags->previous = row.identity;
        tags->source_row++;
    }
    return tags->source_row == count ? accept_bank(tags) : STORE_WORKING;
}

static StoreResult plan(PersonalTags *tags)
{
    for (int step = 0; step < ROWS_PER_STEP && tags->source_row < tags->rows; step++) {
        PersonalTagRow row;
        if (read_row(tags, tags->active_bank, tags->source_row, &row) != STORE_READY) {
            return failed(tags);
        }
        bool replaced = tags->assigning && tags->target_found && tags->source_row == tags->target;
        tags->output_row += !replaced && (row.mask & tags->pending.active) != 0 ? 1u : 0u;
        tags->source_row++;
    }
    if (tags->source_row == tags->rows) {
        if (tags->output_row > PERSONAL_TAG_TRACK_LIMIT) {
            return failed(tags);
        }
        tags->writer.header = (StoreHeader){
            .generation = tags->has_active ? tags->headers[tags->active_bank].generation + 1 : 1,
            .bytes = payload_bytes(tags->output_row),
            .count = (uint16_t)tags->output_row};
        tags->stage = ALLOCATE;
    }
    return STORE_WORKING;
}

static StoreResult allocate(PersonalTags *tags)
{
    StoreFile *file = &tags->storage.files[tags->bank];
    uint32_t size = STORE_BLOCK_BYTES + tags->writer.header.bytes;
    if (!tags->storage.prepare(tags->storage.context, tags->bank, size, file) ||
        file->size < size) {
        return failed(tags);
    }
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    if (!store_file_write(file, 0, block, sizeof block)) {
        return failed(tags);
    }
    tags->position = 0;
    tags->stage = PREFIX;
    return STORE_WORKING;
}

static StoreResult write_catalog(PersonalTags *tags)
{
    uint8_t bytes[STORE_BLOCK_BYTES];
    uint32_t left = PERSONAL_TAG_CATALOG_BYTES - tags->position;
    uint32_t count = left < sizeof bytes ? left : sizeof bytes;
    for (uint32_t index = 0; index < count; index++) {
        uint32_t position = tags->position + index;
        if (position < 4) {
            bytes[index] = (uint8_t)(tags->pending.active >> (position * 8));
        } else {
            uint32_t name = position - 4;
            bytes[index] =
                (uint8_t)tags->pending
                    .names[name / PERSONAL_TAG_NAME_BYTES][name % PERSONAL_TAG_NAME_BYTES];
        }
    }
    if (!store_file_write(&tags->storage.files[tags->bank], STORE_BLOCK_BYTES + tags->position,
                          bytes, count)) {
        return failed(tags);
    }
    tags->position += count;
    if (tags->position == PERSONAL_TAG_CATALOG_BYTES) {
        tags->source_row = 0;
        tags->output_row = 0;
        tags->inserted = !tags->assigning;
        tags->stage = COPY_ROWS;
    }
    return STORE_WORKING;
}

static bool write_row(PersonalTags *tags, const MixIdentity *identity, uint32_t mask)
{
    if (mask == 0) {
        return true;
    }
    uint8_t bytes[PERSONAL_TAG_ROW_BYTES] = {0};
    store_put32(bytes, mix_identity_hash(identity));
    store_put32(bytes + 4, mask);
    if (mix_identity_encode(identity, bytes + 8) == 0 ||
        !store_file_write(&tags->storage.files[tags->bank], row_offset(tags->output_row), bytes,
                          sizeof bytes)) {
        return false;
    }
    tags->output_row++;
    return true;
}

static bool insert_row(PersonalTags *tags)
{
    if (tags->inserted || tags->source_row != tags->target) {
        return true;
    }
    tags->inserted = true;
    return write_row(tags, &tags->identity, tags->mask);
}

static StoreResult copy_rows(PersonalTags *tags)
{
    for (int step = 0; step < ROWS_PER_STEP && tags->source_row < tags->rows; step++) {
        if (!insert_row(tags)) {
            return failed(tags);
        }
        PersonalTagRow row;
        if (read_row(tags, tags->active_bank, tags->source_row, &row) != STORE_READY) {
            return failed(tags);
        }
        bool replaced = tags->assigning && tags->target_found && tags->source_row == tags->target;
        if (!replaced && !write_row(tags, &row.identity, row.mask & tags->pending.active)) {
            return failed(tags);
        }
        tags->source_row++;
    }
    if (tags->source_row == tags->rows) {
        if (!insert_row(tags) || tags->output_row != tags->writer.header.count) {
            return failed(tags);
        }
        const uint32_t *active =
            tags->has_active ? &tags->headers[tags->active_bank].generation : NULL;
        store_write_begin(&tags->writer, active);
        tags->stage = COMMIT;
    }
    return STORE_WORKING;
}

static bool staged_fill(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    PersonalTags *tags = context;
    return store_file_read(&tags->storage.files[tags->bank], STORE_BLOCK_BYTES + position,
                           block->bytes, count);
}

static StoreResult publish(PersonalTags *tags)
{
    StoreSlot slot = bank_slot(tags, tags->bank);
    StoreResult result = store_write_step(&tags->writer, &slot, (StoreFill){staged_fill, tags});
    if (result == STORE_DONE) {
        tags->headers[tags->bank] = tags->writer.header;
        return accept_bank(tags);
    }
    if (result == STORE_UNCERTAIN) {
        tags->load = STORE_UNREADABLE;
    }
    tags->result = result;
    return result;
}

StoreResult personal_tags_step(PersonalTags *tags)
{
    if (tags->result != STORE_WORKING) {
        return tags->result;
    }
    switch (tags->stage) {
    case CHECKSUM:
        return checksum(tags);
    case CATALOG:
        return read_catalog(tags);
    case CHECK_ROWS:
        return check_rows(tags);
    case PLAN:
        return plan(tags);
    case ALLOCATE:
        return allocate(tags);
    case PREFIX:
        return write_catalog(tags);
    case COPY_ROWS:
        return copy_rows(tags);
    case COMMIT:
        return publish(tags);
    }
    return failed(tags);
}

void personal_tags_cancel(PersonalTags *tags)
{
    if (tags->result != STORE_WORKING) {
        return;
    }
    if (tags->stage < PLAN) {
        tags->load = STORE_UNREADABLE;
    }
    store_write_abort(&tags->writer);
    failed(tags);
}

uint8_t personal_tags_progress(const PersonalTags *tags)
{
    if (tags->result == STORE_DONE) {
        return 100;
    }
    if (tags->stage == COMMIT) {
        return (uint8_t)(75 + store_write_progress(&tags->writer) / 4);
    }
    if (tags->stage == CHECKSUM) {
        uint32_t bytes = tags->headers[tags->bank].bytes;
        return bytes == 0 ? 0 : (uint8_t)(tags->scan.position * 50 / bytes);
    }
    uint32_t rows = tags->stage < PLAN ? tags->headers[tags->bank].count : tags->rows;
    uint32_t base = tags->stage == CHECK_ROWS || tags->stage == COPY_ROWS ? 50u : 0u;
    return rows == 0 ? (uint8_t)base : (uint8_t)(base + tags->source_row * 25 / rows);
}
