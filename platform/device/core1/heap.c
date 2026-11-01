#include <stddef.h>

#include "device/core1_heap.h"

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void free(void *pointer);

void *malloc(size_t size)
{
    return core1_heap_allocate(size);
}

void *calloc(size_t count, size_t size)
{
    if (size != 0 && count > (size_t)-1 / size) {
        return NULL;
    }
    unsigned char *block = core1_heap_allocate(count * size);
    for (size_t index = 0; block != NULL && index < count * size; index++) {
        block[index] = 0;
    }
    return block;
}

void free(void *pointer)
{
    (void)pointer;
}
