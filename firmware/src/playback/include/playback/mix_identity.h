#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "playback/store.h"

enum {
    MIX_PATH_BYTES = 259,
    MIX_RECORD_FIELDS = 10,
    MIX_RECORD_MIN = MIX_RECORD_FIELDS + 1,
    MIX_RECORD_MAX = MIX_RECORD_FIELDS + MIX_PATH_BYTES - 1
};

typedef struct MixIdentity {
    char path[MIX_PATH_BYTES];
    uint32_t start_ms;
    uint32_t end_ms;
} MixIdentity;

StoreLoad mix_identity_read(const Library *library, uint16_t track, MixIdentity *identity);
bool mix_identity_equal(const MixIdentity *first, const MixIdentity *second);
uint16_t mix_identity_size(const MixIdentity *identity);
uint16_t mix_identity_encode(const MixIdentity *identity, uint8_t *bytes);
uint16_t mix_identity_decode(const uint8_t *bytes, uint32_t available, MixIdentity *identity);
uint32_t mix_identity_hash(const MixIdentity *identity);
