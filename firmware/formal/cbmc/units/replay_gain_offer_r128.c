#include "formal.h"
#include "replay_gain.c"

void harness(void)
{
    (void)replay_gain_offer_r128(NULL, (ReplayGainField)nondet_uint8(), NULL, nondet_uint32());
}
