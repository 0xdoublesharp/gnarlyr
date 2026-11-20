#include "formal.h"
#include "playback/store.h"

static uint8_t block[STORE_BLOCK_BYTES];

static bool write_nothing(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    (void)offset;
    __CPROVER_assert(__CPROVER_r_ok(bytes, length), "a written span is readable");
    return nondet_bool();
}

void harness(void)
{
    StoreKind kind = {.magic = {'D', 'S', 'T', 'O'}, .version = 1, .inline_payload = nondet_bool()};
    StoreHeader header;
    formal_card_fill();
    StoreFile file = {
        .source = formal_card_source(), .write = write_nothing, .size = formal_card_size};
    StorePair pair = {.file = &file, .kind = &kind, .bank_bytes = nondet_uint32()};
    StoreChoice choice;
    StoreLoad load = store_pair_open(&pair, &choice);
    __CPROVER_assert(load <= STORE_CORRUPT, "the load result is one of the five");
    StoreSlot slot = {.file = &file, .kind = &kind, .offset = nondet_uint32()};
    if (store_load(&slot, block, &header) == STORE_READY && kind.inline_payload) {
        __CPROVER_assert(header.bytes <= STORE_INLINE_BYTES, "an inline payload fits its block");
    }
    StoreScan scan = store_scan_begin();
    scan.position = nondet_uint32();
    uint32_t bytes = nondet_uint32();
    __CPROVER_assume(scan.position <= bytes);
    (void)store_scan_next(&slot, &scan, bytes, block);
}
