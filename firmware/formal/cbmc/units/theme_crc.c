#include "formal.h"
#include "theme_pack.h"

void harness(void)
{
    (void)theme_pack_crc(nondet_uint32(), NULL, nondet_uint32());
}
