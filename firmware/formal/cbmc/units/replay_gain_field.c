#include "formal.h"
#include "replay_gain.c"

void harness(void)
{
    (void)replay_gain_field(NULL, nondet_uint32());
}
