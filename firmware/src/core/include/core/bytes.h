#pragma once

#include <stdint.h>

static inline uint32_t bytes_le16(const uint8_t *bytes)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t value;
    __builtin_memcpy(&value, bytes, sizeof value);
    return value;
#else
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
#endif
}

static inline uint32_t bytes_le32(const uint8_t *bytes)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t value;
    __builtin_memcpy(&value, bytes, sizeof value);
    return value;
#else
    return bytes_le16(bytes) | bytes_le16(bytes + 2) << 16;
#endif
}

static inline uint32_t bytes_be16(const uint8_t *bytes)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16((uint16_t)bytes_le16(bytes));
#else
    return (uint32_t)bytes[0] << 8 | (uint32_t)bytes[1];
#endif
}

static inline uint32_t bytes_be32(const uint8_t *bytes)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(bytes_le32(bytes));
#else
    return bytes_be16(bytes) << 16 | bytes_be16(bytes + 2);
#endif
}

static inline uint32_t bytes_synchsafe(const uint8_t *bytes)
{
    return (uint32_t)(bytes[0] & 0x7F) << 21 | (uint32_t)(bytes[1] & 0x7F) << 14 |
           (uint32_t)(bytes[2] & 0x7F) << 7 | (uint32_t)(bytes[3] & 0x7F);
}

static inline void bytes_put_le16(uint8_t *bytes, uint32_t value)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t short_value = (uint16_t)value;
    __builtin_memcpy(bytes, &short_value, sizeof short_value);
#else
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
#endif
}

static inline void bytes_put_le32(uint8_t *bytes, uint32_t value)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    __builtin_memcpy(bytes, &value, sizeof value);
#else
    bytes_put_le16(bytes, value);
    bytes_put_le16(bytes + 2, value >> 16);
#endif
}

static inline void bytes_put_be32(uint8_t *bytes, uint32_t value)
{
#if defined(__ARM_FEATURE_UNALIGNED) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    bytes_put_le32(bytes, __builtin_bswap32(value));
#else
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
#endif
}
