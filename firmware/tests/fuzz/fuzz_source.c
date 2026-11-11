#include "fuzz_source.h"

#include <string.h>

static bool fuzz_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FuzzSource *source = context;
    if (offset > source->size || length > source->size - offset) {
        return false;
    }
    if (length > 0) {
        memcpy(buffer, source->bytes + offset, length);
    }
    return true;
}

ByteSource fuzz_source(FuzzSource *source)
{
    return (ByteSource){.read = fuzz_read, .context = source};
}

void fuzz_require(bool condition)
{
    if (!condition) {
        __builtin_trap();
    }
}
