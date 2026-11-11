#ifndef OPUS_BOUNDED_SUPPORT_H
#define OPUS_BOUNDED_SUPPORT_H

#include <stddef.h>

#define STACK_ALLOC_H
#define OVERRIDE_OPUS_ALLOC
#define OVERRIDE_OPUS_REALLOC
#define OVERRIDE_OPUS_FREE
#define OVERRIDE_OPUS_ALLOC_SCRATCH
#define OVERRIDE_celt_fatal

void *opus_alloc(size_t bytes);
void *opus_realloc(void *pointer, size_t bytes);
void opus_free(void *pointer);
void *opus_alloc_scratch(size_t bytes);
void *opus_decode_allocate(size_t count, size_t bytes, size_t alignment);
void *opus_decode_mark(void);
void opus_decode_restore(void *mark);
_Noreturn void celt_fatal(const char *message, const char *file, int line);

static inline int opus_decode_unchecked(void)
{
    return 0;
}

#define VARDECL(type, var) type *var
#define ALLOC(var, count, type)                                                                    \
    var = opus_decode_allocate((size_t)(count), sizeof(type), _Alignof(type))
#define SAVE_STACK void *_saved_stack = opus_decode_mark();
#define ALLOC_STACK SAVE_STACK
#define RESTORE_STACK opus_decode_restore(_saved_stack)
#define ALLOC_NONE 0
#define OPUS_CHECK_ARRAY(pointer, length) opus_decode_unchecked()
#define OPUS_CHECK_VALUE(value) opus_decode_unchecked()
#define OPUS_CHECK_ARRAY_COND(pointer, length) opus_decode_unchecked()
#define OPUS_CHECK_VALUE_COND(value) opus_decode_unchecked()
#define OPUS_PRINT_INT(value)                                                                      \
    do {                                                                                           \
    } while (0)
#define OPUS_FPRINTF (void)

#endif
