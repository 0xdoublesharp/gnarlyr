#pragma once

#include "playback/mix_tape.h"

uint16_t mix_identity_encode(const MixIdentity *identity, uint8_t *bytes);
bool mix_identity_valid(const MixIdentity *identity);
