#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/hash.h"
#include "fuzz_source.h"
#include "playback/store.h"

enum {
    MAX_PAYLOAD = 2048,
    BANK_BYTES = STORE_BLOCK_BYTES + MAX_PAYLOAD,
    DISK_BYTES = 2 * BANK_BYTES,
    FAULT_COUNT = 5
};

static const StoreKind BANKED = {.magic = {'D', 'S', 'F', 'B'}, .version = 3};
static const StoreKind INLINE = {
    .magic = {'D', 'S', 'F', 'I'}, .version = 1, .inline_payload = true};

typedef struct FuzzInput {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
} FuzzInput;

typedef struct FuzzDisk {
    uint8_t bytes[DISK_BYTES];
    uint32_t operations;
    uint32_t fault_at;
    uint8_t fault;
    bool fault_enabled;
    bool invalid_io;
} FuzzDisk;

typedef struct Payload {
    const uint8_t *bytes;
    uint32_t size;
} Payload;

typedef struct PairContext {
    const FuzzDisk *disk;
} PairContext;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t next_value(FuzzInput *input)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < 4 && input->offset < input->size; index++) {
        value |= (uint32_t)input->bytes[input->offset++] << (8u * index);
    }
    return value;
}

static bool fault_now(FuzzDisk *disk)
{
    bool selected = disk->fault_enabled && disk->fault != 0 && disk->operations == disk->fault_at;
    disk->operations++;
    return selected;
}

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    FuzzDisk *disk = context;
    if (length > STORE_BLOCK_BYTES || offset > sizeof disk->bytes ||
        length > sizeof disk->bytes - offset) {
        disk->invalid_io = true;
        return false;
    }
    if (fault_now(disk)) {
        return false;
    }
    if (length > 0) {
        memcpy(bytes, disk->bytes + offset, length);
    }
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    FuzzDisk *disk = context;
    if (length > STORE_BLOCK_BYTES || offset > sizeof disk->bytes ||
        length > sizeof disk->bytes - offset) {
        disk->invalid_io = true;
        return false;
    }
    if (!fault_now(disk)) {
        memcpy(disk->bytes + offset, bytes, length);
        return true;
    }
    if (disk->fault == 1) {
        return false;
    }
    uint32_t amount = disk->fault == 2 ? length / 2 : length;
    memcpy(disk->bytes + offset, bytes, amount);
    if (disk->fault == 4 && amount > 0) {
        disk->bytes[offset + amount / 2] ^= 1;
    }
    return disk->fault == 4;
}

static StoreFile store_file(FuzzDisk *disk)
{
    return (StoreFile){.source = {read_disk, disk}, .write = write_disk, .size = DISK_BYTES};
}

static void make_payload(uint8_t *bytes, uint32_t length, uint32_t seed)
{
    uint32_t value = seed;
    for (uint32_t index = 0; index < length; index++) {
        value = value * UINT32_C(1664525) + UINT32_C(1013904223);
        bytes[index] = (uint8_t)(value >> 24) ^ (uint8_t)index;
    }
}

static uint32_t payload_length(uint32_t value)
{
    static const uint32_t BOUNDARIES[] = {0, 1, 511, 512, 513, MAX_PAYLOAD - 1, MAX_PAYLOAD};
    uint32_t selected = value & 15u;
    return selected < sizeof BOUNDARIES / sizeof BOUNDARIES[0] ? BOUNDARIES[selected]
                                                               : value % (MAX_PAYLOAD + 1u);
}

static bool fill_payload(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    const Payload *payload = context;
    if (position > payload->size || count > payload->size - position) {
        return false;
    }
    memcpy(block->bytes, payload->bytes + position, count);
    return true;
}

static StoreHeader value_header(uint32_t generation, uint32_t bytes, uint32_t binding,
                                uint16_t count)
{
    return (StoreHeader){
        .generation = generation, .bytes = bytes, .binding = binding, .count = count};
}

static void put_value(FuzzDisk *disk, uint8_t bank, StoreHeader header, const uint8_t *payload)
{
    uint32_t at = (uint32_t)bank * BANK_BYTES;
    memcpy(disk->bytes + at + STORE_BLOCK_BYTES, payload, header.bytes);
    header.hash = hash_update(HASH_START, payload, header.bytes);
    store_encode(&BANKED, &header, disk->bytes + at);
}

static bool same_header(const StoreHeader *first, const StoreHeader *second)
{
    return first->generation == second->generation && first->bytes == second->bytes &&
           first->hash == second->hash && first->binding == second->binding &&
           first->count == second->count;
}

static StoreLoad check_bank(void *context, uint8_t bank, const StoreHeader *header)
{
    const PairContext *pair = context;
    if (header->bytes > MAX_PAYLOAD) {
        return STORE_CORRUPT;
    }
    uint32_t at = (uint32_t)bank * BANK_BYTES + STORE_BLOCK_BYTES;
    uint32_t hash = hash_update(HASH_START, pair->disk->bytes + at, header->bytes);
    return hash == header->hash ? STORE_READY : STORE_CORRUPT;
}

static StoreLoad open_pair(StoreFile *file, PairContext *context, StoreChoice *choice)
{
    StorePair pair = {.file = file,
                      .kind = &BANKED,
                      .bank_bytes = BANK_BYTES,
                      .check = check_bank,
                      .context = context};
    return store_pair_open(&pair, choice);
}

static bool choice_matches(const FuzzDisk *disk, const StoreChoice *choice, uint8_t bank,
                           const Payload *payload)
{
    uint32_t at = (uint32_t)bank * BANK_BYTES + STORE_BLOCK_BYTES;
    return choice->bank == bank && choice->header.bytes == payload->size &&
           memcmp(disk->bytes + at, payload->bytes, payload->size) == 0;
}

static StoreResult run_write(StoreWrite *job, const StoreSlot *slot, StoreFill fill, uint32_t bytes)
{
    uint32_t blocks = (bytes + STORE_BLOCK_BYTES - 1u) / STORE_BLOCK_BYTES;
    uint32_t limit = 2u * blocks + 4u;
    uint8_t progress = store_write_progress(job);
    for (uint32_t step = 0; step < limit && job->result == STORE_WORKING; step++) {
        StoreResult result = store_write_step(job, slot, fill);
        uint8_t next = store_write_progress(job);
        fuzz_require(next >= progress && next <= 100);
        fuzz_require(result == job->result);
        progress = next;
    }
    fuzz_require(job->result != STORE_WORKING);
    return job->result;
}

static void check_write(FuzzInput *input)
{
    FuzzDisk disk = {0};
    uint8_t old_bytes[MAX_PAYLOAD];
    uint8_t new_bytes[MAX_PAYLOAD];
    uint8_t old_bank[BANK_BYTES];
    uint32_t old_generation = next_value(input);
    uint32_t length = payload_length(next_value(input));
    uint32_t binding = next_value(input);
    uint16_t count = (uint16_t)next_value(input);
    make_payload(old_bytes, 513, old_generation ^ binding);
    make_payload(new_bytes, length, next_value(input));
    StoreHeader old_header = value_header(old_generation, 513, binding ^ 1u, count);
    StoreHeader new_header = value_header(old_generation + 1u, length, binding, count);
    put_value(&disk, 0, old_header, old_bytes);
    memcpy(old_bank, disk.bytes, sizeof old_bank);
    disk.fault = (uint8_t)(next_value(input) % FAULT_COUNT);
    disk.fault_at = next_value(input) % 16u;
    disk.fault_enabled = true;
    StoreFile file = store_file(&disk);
    StoreSlot slot = {.file = &file, .kind = &BANKED, .offset = BANK_BYTES};
    StoreWrite job = {.header = new_header};
    store_write_begin(&job, &old_generation);
    Payload old_payload = {.bytes = old_bytes, .size = 513};
    Payload new_payload = {.bytes = new_bytes, .size = length};
    StoreResult result = run_write(&job, &slot, (StoreFill){fill_payload, &new_payload}, length);
    fuzz_require(!disk.invalid_io && memcmp(old_bank, disk.bytes, sizeof old_bank) == 0);
    disk.fault_enabled = false;
    PairContext context = {.disk = &disk};
    StoreChoice choice;
    fuzz_require(open_pair(&file, &context, &choice) == STORE_READY);
    bool old_ready = choice_matches(&disk, &choice, 0, &old_payload);
    bool new_ready = choice_matches(&disk, &choice, 1, &new_payload);
    fuzz_require(old_ready || new_ready);
    fuzz_require(choice.header.generation ==
                 (new_ready ? new_header.generation : old_header.generation));
    fuzz_require(choice.header.binding == (new_ready ? new_header.binding : old_header.binding));
    fuzz_require(choice.header.count == count);
    fuzz_require(result == STORE_DONE || result == STORE_FAILED || result == STORE_UNCERTAIN);
    fuzz_require(result != STORE_DONE || new_ready);
    fuzz_require(result != STORE_FAILED || old_ready);
    fuzz_require(!disk.invalid_io);
}

static void check_decode(const uint8_t *data, size_t size)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    size_t copied = size < sizeof block ? size : sizeof block;
    if (copied > 0) {
        memcpy(block, data, copied);
    }
    StoreHeader first;
    StoreHeader second;
    StoreLoad left = store_decode(&BANKED, block, &first);
    StoreLoad right = store_decode(&BANKED, block, &second);
    fuzz_require(left == right && same_header(&first, &second));
    fuzz_require(left >= STORE_EMPTY && left <= STORE_CORRUPT);
}

static void check_round_trip(FuzzInput *input)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    StoreHeader external = value_header(next_value(input), next_value(input), next_value(input),
                                        (uint16_t)next_value(input));
    external.hash = next_value(input);
    StoreHeader decoded;
    store_encode(&BANKED, &external, block);
    fuzz_require(store_decode(&BANKED, block, &decoded) == STORE_READY);
    fuzz_require(same_header(&external, &decoded));
    uint32_t length = next_value(input) % (STORE_INLINE_BYTES + 1u);
    make_payload(block + STORE_FIELD_BYTES, length, next_value(input));
    StoreHeader inlined =
        value_header(next_value(input), length, next_value(input), (uint16_t)next_value(input));
    store_encode(&INLINE, &inlined, block);
    fuzz_require(store_decode(&INLINE, block, &decoded) == STORE_READY);
    inlined.hash = hash_update(HASH_START, block + STORE_FIELD_BYTES, length);
    fuzz_require(same_header(&inlined, &decoded));
    uint32_t first = next_value(input);
    uint32_t second = next_value(input);
    fuzz_require(!(store_newer(first, second) && store_newer(second, first)));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    check_decode(data, size);
    FuzzInput input = {.bytes = data, .size = size};
    check_round_trip(&input);
    check_write(&input);
    return 0;
}
