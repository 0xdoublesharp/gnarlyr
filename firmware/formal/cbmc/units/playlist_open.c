#include "formal.h"
#include "playlist_reader.c"

void harness(void)
{
    (void)playlist_reader_open(NULL, formal_card_source(), nondet_uint32());
}
