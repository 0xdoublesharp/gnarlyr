#pragma once

#include "formal.h"
#include "theme_pack.h"

#define FORMAL_CONTRACT_theme_pack_crc                                                             \
    __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(bytes, length))         \
        __CPROVER_assigns()

#define FORMAL_LOOP_theme_pack_crc_0                                                               \
    __CPROVER_assigns(index, crc) __CPROVER_loop_invariant(index <= length)                        \
        __CPROVER_decreases(length - index)

#define FORMAL_CONTRACT_zeroes                                                                     \
    __CPROVER_requires(length <= FORMAL_OBJECT_LIMIT && __CPROVER_is_fresh(bytes, length))         \
        __CPROVER_assigns()                                                                        \
            __CPROVER_ensures(!__CPROVER_return_value || length == 0 || bytes[length - 1] == 0)

#define FORMAL_LOOP_zeroes_0                                                                       \
    __CPROVER_assigns(at)                                                                          \
        __CPROVER_loop_invariant(at <= length && (at == 0 || bytes[at - 1] == 0))                  \
            __CPROVER_decreases(length - at)

#define FORMAL_CONTRACT_text_valid                                                                 \
    __CPROVER_requires(length >= 1 && length <= FORMAL_OBJECT_LIMIT)                               \
        __CPROVER_requires(__CPROVER_is_fresh(bytes, length)) __CPROVER_assigns()                  \
            __CPROVER_ensures(!__CPROVER_return_value || bytes[length - 1] == 0)

#define FORMAL_LOOP_text_valid_0                                                                   \
    __CPROVER_assigns(end) __CPROVER_loop_invariant(end <= length) __CPROVER_decreases(length - end)

#define FORMAL_LOOP_text_valid_1                                                                   \
    __CPROVER_assigns(cursor) __CPROVER_loop_invariant(                                            \
        __CPROVER_same_object(cursor, bytes) &&                                                    \
        __CPROVER_POINTER_OFFSET(cursor) >= __CPROVER_POINTER_OFFSET(bytes) &&                     \
        __CPROVER_POINTER_OFFSET(cursor) <= __CPROVER_POINTER_OFFSET(bytes) + end)                 \
        __CPROVER_decreases((int64_t)__CPROVER_POINTER_OFFSET(bytes) + end -                       \
                            (int64_t)__CPROVER_POINTER_OFFSET(cursor))

#define FORMAL_CONTRACT_url_valid                                                                  \
    __CPROVER_requires(__CPROVER_is_fresh(bytes, 128)) __CPROVER_assigns()

#define FORMAL_CONTRACT_version_valid                                                              \
    __CPROVER_requires(__CPROVER_is_fresh(bytes, 32) && bytes[31] == 0) __CPROVER_assigns()

#define FORMAL_CONTRACT_header_valid                                                               \
    __CPROVER_requires(__CPROVER_is_fresh(bytes, THEME_PACK_HEADER_BYTES)) __CPROVER_assigns()     \
        __CPROVER_ensures(!__CPROVER_return_value ||                                               \
                          (bytes[19] <= THEME_PACK_MAX_ASSETS && bytes[36 + 31] == 0 &&            \
                           bytes[100 + 31] == 0 && store_get32(bytes + 8) == size &&               \
                           (store_get16(bytes + 492) == 0 ||                                       \
                            store_get16(bytes + 492) == THEME_PACK_SIGNATURE_BYTES)))

#define FORMAL_CONTRACT_crc_valid                                                                  \
    __CPROVER_requires(source.read == formal_card_read)                                            \
        __CPROVER_requires(size >= THEME_PACK_HEADER_BYTES && size <= THEME_PACK_MAX_BYTES)        \
            __CPROVER_assigns()

#define FORMAL_LOOP_crc_valid_0                                                                    \
    __CPROVER_assigns(offset, crc, __CPROVER_object_whole(block))                                  \
        __CPROVER_loop_invariant(offset <= size) __CPROVER_decreases(size - offset)

#define FORMAL_CONTRACT_decode_palette                                                             \
    __CPROVER_requires(__CPROVER_is_fresh(pack, sizeof(ThemePack)))                                \
        __CPROVER_requires(__CPROVER_is_fresh(header, THEME_PACK_HEADER_BYTES))                    \
            __CPROVER_assigns(pack->palette, pack->low_contrast)
