#include "formal.h"
#include "playlist_reader.c"

void harness(void)
{
    (void)playlist_reader_next(NULL, NULL, nondet_uint32());
}
