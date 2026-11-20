#include "device/core1_heap.h"

#include "device/core1_arena.h"

static Core1Arena heap;

void core1_heap_init(void *base, size_t bytes)
{
    core1_arena_init(&heap, base, (uint32_t)bytes);
}

void core1_heap_reset(void)
{
    core1_arena_reset(&heap);
}

void *core1_heap_allocate(size_t bytes)
{
    return core1_arena_allocate(&heap, bytes);
}
