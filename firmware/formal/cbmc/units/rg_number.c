#include "formal.h"
#include "replay_gain.c"

void harness(void)
{
    (void)number_of(NULL, nondet_uint32(), nondet_bool(), NULL);
}
