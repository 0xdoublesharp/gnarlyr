#include "device/core1_arena.h"

void *core1_arena_allocate(Core1Arena *arena, size_t bytes)
{
    size_t rounded = (bytes + 7u) & ~(size_t)7u;
    if (rounded < bytes || rounded > arena->high - arena->low) {
        return NULL;
    }
    void *block = arena->base + arena->low;
    arena->low += (uint32_t)rounded;
    core1_arena_note_peak(arena);
    return block;
}
