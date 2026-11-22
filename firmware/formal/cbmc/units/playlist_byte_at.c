#include "formal.h"
#include "playlist_reader.c"

void harness(void)
{
    (void)byte_at(NULL, nondet_uint32(), NULL);
}
