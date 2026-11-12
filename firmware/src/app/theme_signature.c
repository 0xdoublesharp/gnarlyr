#include "theme_signature.h"

#include "../../third_party/monocypher/monocypher-ed25519.h"
#include "hal/audio.h"
#include "theme_pack.h"
#include "theme_trusted_keys.h"

bool theme_signature_verify(ByteSource source, uint32_t size, const uint8_t signature[64],
                            const uint8_t public_key[32])
{
    crypto_sha512_ctx hash;
    crypto_sha512_init(&hash);
    uint8_t block[512];
    for (uint32_t offset = 0; offset < size;) {
        uint32_t count = size - offset < sizeof block ? size - offset : sizeof block;
        if (!source.read(source.context, offset, block, count)) {
            return false;
        }
        crypto_sha512_update(&hash, block, count);
        offset += count;
        hal_audio_service();
    }
    uint8_t digest[64];
    crypto_sha512_final(&hash, digest);
    return crypto_ed25519_ph_check(signature, public_key, digest) == 0;
}

#if THEME_TRUSTED_KEY_COUNT > 0
static bool same_key(const uint8_t *first, const uint8_t *second)
{
    uint8_t difference = 0;
    for (int at = 0; at < 16; at++) {
        difference |= first[at] ^ second[at];
    }
    return difference == 0;
}

#endif

bool theme_signature_inspect(ByteSource source, uint32_t end, uint32_t length, bool *verified)
{
    *verified = false;
    if (length == 0) {
        return true;
    }
    uint8_t block[THEME_PACK_SIGNATURE_BYTES];
    if (length != sizeof block || !source.read(source.context, end, block, sizeof block) ||
        block[0] != 'D' || block[1] != 'S' || block[2] != 'S' || block[3] != 'G' || block[4] != 1 ||
        block[5] != 0 || block[6] != 0 || block[7] != 0) {
        return false;
    }
#if THEME_TRUSTED_KEY_COUNT > 0
    for (unsigned at = 0; at < THEME_TRUSTED_KEY_COUNT; at++) {
        if (same_key(block + 8, THEME_TRUSTED_KEYS[at].id)) {
            *verified =
                theme_signature_verify(source, end, block + 24, THEME_TRUSTED_KEYS[at].public_key);
            break;
        }
    }
#endif
    return true;
}
