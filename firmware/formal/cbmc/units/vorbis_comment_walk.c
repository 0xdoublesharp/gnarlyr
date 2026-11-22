#include "formal.h"
#include "tags_vorbis.c"

void harness(void)
{
    ByteSource source = {.read = nondet_bool() ? formal_card_read : read_file_bytes,
                         .context = NULL};
    (void)tags_read_vorbis_comment(NULL, source, nondet_uint32(), nondet_uint32());
}
