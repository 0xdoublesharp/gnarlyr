#include "device/vorbis_memory.h"

static VorbisMemory *active;

bool vorbis_memory_init(VorbisMemory *memory, void *base, uint32_t capacity)
{
    if (base == NULL || (uintptr_t)base % 8 != 0 || capacity < 8) {
        return false;
    }
    uint32_t aligned = capacity & ~7u;
    *memory = (VorbisMemory){.base = base, .capacity = aligned, .temporary = aligned};
    return true;
}

bool vorbis_memory_bind(VorbisMemory *memory)
{
    if (active != NULL || memory->base == NULL || memory->error != VORBIS_MEMORY_OK) {
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

static uint32_t rounded_size(size_t bytes)
{
    if (active == NULL) {
        __builtin_trap();
    }
    if (bytes > UINT32_MAX - 7u) {
        leave_guard(VORBIS_MEMORY_EXHAUSTED);
    }
    uint32_t rounded = ((uint32_t)bytes + 7u) & ~7u;
    rounded = rounded == 0 ? 8 : rounded;
    if (rounded > active->temporary - active->used) {
        leave_guard(VORBIS_MEMORY_EXHAUSTED);
    }
    return rounded;
}

static void record_peak(void)
{
    uint32_t bytes = active->used + (active->capacity - active->temporary);
    if (bytes > active->peak) {
        active->peak = bytes;
    }
}

void *vorbis_memory_allocate(size_t bytes)
{
    uint32_t rounded = rounded_size(bytes);
    void *block = active->base + active->used;
    active->used += rounded;
    record_peak();
    return block;
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
    uint32_t rounded = rounded_size(bytes);
    active->temporary -= rounded;
    record_peak();
    return active->base + active->temporary;
}

uint32_t vorbis_temporary_mark(void)
{
    return active->temporary;
}

void vorbis_temporary_restore(const uint32_t *mark)
{
    active->temporary = *mark;
}

bool vorbis_memory_requires_bits(void)
{
    return active->strict_bits;
}

_Noreturn void vorbis_memory_invalid(void)
{
    leave_guard(VORBIS_MEMORY_INVALID);
}
