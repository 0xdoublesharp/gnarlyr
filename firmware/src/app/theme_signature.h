#pragma once

#include "library/byte_source.h"

typedef struct ThemeTrustedKey {
    uint8_t id[16];
    uint8_t public_key[32];
} ThemeTrustedKey;

bool theme_signature_verify(ByteSource source, uint32_t size, const uint8_t signature[64],
                            const uint8_t public_key[32]);
bool theme_signature_inspect(ByteSource source, uint32_t end, uint32_t length, bool *verified);
