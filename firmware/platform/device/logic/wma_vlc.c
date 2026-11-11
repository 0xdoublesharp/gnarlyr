#include <string.h>

#include "ffmpeg_get_bits.h"

typedef struct WmaCodes {
    const uint8_t *bits;
    const uint8_t *codes;
    int bits_wrap;
    int bits_size;
    int codes_wrap;
    int codes_size;
    int count;
} WmaCodes;

static uint32_t value(const uint8_t *bytes, int size)
{
    uint32_t result = 0;
    if (size != 1 && size != 2 && size != 4) {
        wma_fail();
    }
    memcpy(&result, bytes, (size_t)size);
    return result;
}

static uint32_t length(const WmaCodes *codes, int index)
{
    return value(codes->bits + index * codes->bits_wrap, codes->bits_size);
}

static uint32_t code_at(const WmaCodes *codes, int index)
{
    return value(codes->codes + index * codes->codes_wrap, codes->codes_size);
}

static int allocate_table(VLC *vlc, uint32_t bits)
{
    int count = 1 << bits;
    if (vlc->table_size > vlc->table_allocated - count) {
        wma_fail();
    }
    int start = vlc->table_size;
    for (int index = 0; index < count; index++) {
        vlc->table[start + index][0] = -1;
        vlc->table[start + index][1] = 0;
    }
    vlc->table_size += count;
    return start;
}

static uint32_t child_bits(const WmaCodes *codes, uint32_t prefix, uint32_t used, uint32_t root)
{
    uint32_t longest = 0;
    for (int index = 0; index < codes->count; index++) {
        uint32_t len = length(codes, index);
        if (len > used && len <= 32 && (code_at(codes, index) >> (len - used)) == prefix) {
            uint32_t left = len - used;
            longest = left > longest ? left : longest;
        }
    }
    if (longest == 0) {
        wma_fail();
    }
    return longest < root ? longest : root;
}

static void insert(VLC *vlc, const WmaCodes *codes, int symbol)
{
    uint32_t len = length(codes, symbol);
    uint32_t code = code_at(codes, symbol);
    uint32_t used = 0;
    uint32_t width = (uint32_t)vlc->bits;
    int base = 0;
    if (len > 32) {
        wma_fail();
    }
    while (used < len) {
        uint32_t remaining = len - used;
        if (remaining <= width) {
            uint32_t suffix = code & (remaining == 32 ? UINT32_MAX : (1u << remaining) - 1u);
            uint32_t first = suffix << (width - remaining);
            for (uint32_t index = 0; index < (1u << (width - remaining)); index++) {
                vlc->table[base + (int)(first + index)][0] = (int16_t)symbol;
                vlc->table[base + (int)(first + index)][1] = (int16_t)remaining;
            }
            return;
        }
        uint32_t index = (code >> (remaining - width)) & ((1u << width) - 1u);
        used += width;
        int at = base + (int)index;
        if (vlc->table[at][1] == 0) {
            uint32_t prefix = code >> (len - used);
            uint32_t child = child_bits(codes, prefix, used, (uint32_t)vlc->bits);
            vlc->table[at][0] = (int16_t)allocate_table(vlc, child);
            vlc->table[at][1] = -(int16_t)child;
        }
        if (vlc->table[at][1] >= 0) {
            wma_fail();
        }
        base = vlc->table[at][0];
        width = (uint32_t)-vlc->table[at][1];
    }
}

int init_vlc_sparse(VLC *vlc, int nb_bits, int nb_codes, const void *bits, int bits_wrap,
                    int bits_size, const void *codes, int codes_wrap, int codes_size,
                    const void *symbols, int symbols_wrap, int symbols_size, int flags)
{
    (void)symbols_wrap;
    (void)symbols_size;
    if (nb_bits < 1 || nb_bits > 9 || nb_codes < 1 || nb_codes > 1336 || symbols != NULL ||
        flags != INIT_VLC_USE_NEW_STATIC) {
        wma_fail();
    }
    WmaCodes set = {bits, codes, bits_wrap, bits_size, codes_wrap, codes_size, nb_codes};
    vlc->bits = nb_bits;
    vlc->table_size = 0;
    (void)allocate_table(vlc, (uint32_t)nb_bits);
    for (int index = 0; index < nb_codes; index++) {
        insert(vlc, &set, index);
    }
    return 0;
}
