#include "formal.h"
#include "tags_vorbis.c"

void harness(void)
{
    read_flac_picture(NULL, nondet_uint32(), nondet_uint32());
}
