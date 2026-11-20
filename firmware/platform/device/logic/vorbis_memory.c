#include "device/vorbis_memory.h"

static VorbisMemory *active;

bool vorbis_memory_init(VorbisMemory *memory, void *base, uint32_t capacity)
{
    if (base == NULL || (uintptr_t)base % 8 != 0 || capacity < 8) {
        return false;
    }
    *memory = (VorbisMemory){0};
    core1_arena_init(&memory->arena, base, capacity & ~7u);
    return true;
}

bool vorbis_memory_bind(VorbisMemory *memory)
{
    if (active != NULL || memory->arena.base == NULL || memory->error != VORBIS_MEMORY_OK) {
        return false;
    }
    active = memory;
    return true;
}

void vorbis_memory_unbind(void)
{
    active = NULL;
}

static _Noreturn void leave_guard(VorbisMemoryError error)
{
    if (active == NULL) {
        __builtin_trap();
    }
    active->error = error;
#if defined(__arm__) && defined(__GNUC__)
    __builtin_longjmp(active->checkpoint, 1);
#else
    longjmp(active->checkpoint, 1);
#endif
}

static Core1Arena *bound_arena(void)
{
    if (active == NULL) {
        __builtin_trap();
    }
    return &active->arena;
}

static void *granted(void *block)
{
    if (block == NULL) {
        leave_guard(VORBIS_MEMORY_EXHAUSTED);
    }
    return block;
}

void *vorbis_memory_allocate(size_t bytes)
{
    return granted(core1_arena_allocate(bound_arena(), bytes == 0 ? 1 : bytes));
}

void *vorbis_memory_calloc(size_t count, size_t bytes)
{
    if (bytes != 0 && count > (size_t)-1 / bytes) {
        leave_guard(VORBIS_MEMORY_EXHAUSTED);
    }
    size_t length = count * bytes;
    uint8_t *block = vorbis_memory_allocate(length);
    for (size_t index = 0; index < length; index++) {
        block[index] = 0;
    }
    return block;
}

void vorbis_memory_free(void *pointer)
{
    (void)pointer;
}

void *vorbis_temporary_allocate(size_t bytes)
{
    return granted(core1_arena_push(bound_arena(), bytes == 0 ? 1 : bytes, 8));
}

uint32_t vorbis_temporary_mark(void)
{
    return core1_arena_mark(&active->arena);
}

void vorbis_temporary_restore(const uint32_t *mark)
{
    (void)core1_arena_restore(&active->arena, *mark);
}

bool vorbis_memory_requires_bits(void)
{
    return active->strict_bits;
}

_Noreturn void vorbis_memory_invalid(void)
{
    leave_guard(VORBIS_MEMORY_INVALID);
}
