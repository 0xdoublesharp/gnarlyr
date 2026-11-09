#include "device/core1_heap.h"

enum {
    HEAP_ALIGN = 8
};

static uint8_t *heap_base;
static size_t heap_bytes;
static size_t heap_used;

void core1_heap_init(void *base, size_t bytes)
{
    heap_base = base;
    heap_bytes = bytes;
    heap_used = 0;
}

void core1_heap_reset(void)
{
    heap_used = 0;
}

void *core1_heap_allocate(size_t bytes)
{
    size_t rounded = (bytes + HEAP_ALIGN - 1) / HEAP_ALIGN * HEAP_ALIGN;
    if (heap_base == NULL || rounded < bytes || rounded > heap_bytes - heap_used) {
        return NULL;
    }
    void *block = heap_base + heap_used;
    heap_used += rounded;
    return block;
}
