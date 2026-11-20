#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Core1Arena {
    uint8_t *base;
    uint32_t capacity;
    uint32_t low;
    uint32_t high;
    uint32_t peak;
} Core1Arena;

void *core1_arena_allocate(Core1Arena *arena, size_t bytes);

static inline void core1_arena_init(Core1Arena *arena, void *base, uint32_t capacity)
{
    *arena = (Core1Arena){.base = base, .capacity = capacity, .high = capacity};
}

static inline void core1_arena_reset(Core1Arena *arena)
{
    arena->low = 0;
    arena->high = arena->capacity;
}

static inline void core1_arena_note_peak(Core1Arena *arena)
{
    uint32_t used = arena->low + (arena->capacity - arena->high);
    if (used > arena->peak) {
        arena->peak = used;
    }
}

static inline void *core1_arena_push(Core1Arena *arena, size_t bytes, size_t alignment)
{
    uintptr_t floor = (uintptr_t)arena->base + arena->low;
    uintptr_t top = (uintptr_t)arena->base + arena->high;
    if (bytes > top - floor) {
        return NULL;
    }
    uintptr_t start = (top - bytes) & ~(uintptr_t)(alignment - 1);
    if (start < floor) {
        return NULL;
    }
    arena->high = (uint32_t)(start - (uintptr_t)arena->base);
    core1_arena_note_peak(arena);
    return (void *)start;
}

static inline uint32_t core1_arena_mark(const Core1Arena *arena)
{
    return arena->high;
}

static inline bool core1_arena_restore(Core1Arena *arena, uint32_t mark)
{
    if (mark < arena->high || mark > arena->capacity) {
        return false;
    }
    arena->high = mark;
    return true;
}
