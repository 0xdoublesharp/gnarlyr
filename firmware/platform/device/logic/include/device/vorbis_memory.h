#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__arm__) && defined(__GNUC__)
typedef intptr_t VorbisCheckpoint[5];
#define VORBIS_MEMORY_TRY(memory) __builtin_setjmp((memory)->checkpoint)
#else
#include <setjmp.h>
typedef jmp_buf VorbisCheckpoint;
#define VORBIS_MEMORY_TRY(memory) setjmp((memory)->checkpoint)
#endif

typedef enum VorbisMemoryError {
    VORBIS_MEMORY_OK,
    VORBIS_MEMORY_EXHAUSTED,
    VORBIS_MEMORY_INVALID
} VorbisMemoryError;

typedef struct VorbisMemory {
    uint8_t *base;
    uint32_t capacity;
    uint32_t used;
    uint32_t temporary;
    uint32_t peak;
    VorbisMemoryError error;
    bool strict_bits;
    VorbisCheckpoint checkpoint;
} VorbisMemory;

bool vorbis_memory_init(VorbisMemory *memory, void *base, uint32_t capacity);
bool vorbis_memory_bind(VorbisMemory *memory);
void vorbis_memory_unbind(void);
void *vorbis_memory_allocate(size_t bytes);
void *vorbis_memory_calloc(size_t count, size_t bytes);
void vorbis_memory_free(void *pointer);
void *vorbis_temporary_allocate(size_t bytes);
uint32_t vorbis_temporary_mark(void);
void vorbis_temporary_restore(const uint32_t *mark);
bool vorbis_memory_requires_bits(void);
_Noreturn void vorbis_memory_invalid(void);

#define VORBIS_TEMPORARY_SCOPE                                                                     \
    uint32_t vorbis_temporary_scope_mark __attribute__((cleanup(vorbis_temporary_restore))) =      \
        vorbis_temporary_mark()
