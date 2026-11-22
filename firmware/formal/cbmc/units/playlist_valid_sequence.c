#include "formal.h"
#include "playlist_reader.c"

void harness(void)
{
    (void)valid_sequence(NULL, nondet_uint32(), nondet_uint8(), nondet_uint32());
}
