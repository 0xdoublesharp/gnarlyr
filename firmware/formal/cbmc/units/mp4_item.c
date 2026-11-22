#include "formal.h"
#include "tags_mp4.c"

static Mp4Atom fallback;

void harness(void)
{
    read_item(NULL, NULL, nondet_bool() ? &fallback : NULL);
}
