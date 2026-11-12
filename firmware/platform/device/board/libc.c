#include "libc.h"

#include <stdbool.h>
#include <stdint.h>

#define WORD_BYTES 4u
#define WORD_MASK 3u
#define BYTE_SPREAD 0x01010101u

typedef uint32_t __attribute__((may_alias)) Word;

static bool words_line_up(const void *to, const void *from)
{
    return (((uintptr_t)to ^ (uintptr_t)from) & WORD_MASK) == 0;
}

static void copy_words_up(Word *to, const Word *from, size_t words)
{
    while (words-- > 0) {
        *to++ = *from++;
    }
}

static void copy_words_down(Word *end, const Word *from_end, size_t words)
{
    while (words-- > 0) {
        *--end = *--from_end;
    }
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    if (words_line_up(to, from)) {
        for (; count > 0 && ((uintptr_t)to & WORD_MASK) != 0; count--) {
            *to++ = *from++;
        }
        size_t words = count / WORD_BYTES;
        copy_words_up((Word *)to, (const Word *)from, words);
        to += words * WORD_BYTES;
        from += words * WORD_BYTES;
        count -= words * WORD_BYTES;
    }
    while (count-- > 0) {
        *to++ = *from++;
    }
    return destination;
}

static void move_down(uint8_t *to, const uint8_t *from, size_t count)
{
    to += count;
    from += count;
    if (words_line_up(to, from)) {
        for (; count > 0 && ((uintptr_t)to & WORD_MASK) != 0; count--) {
            *--to = *--from;
        }
        size_t words = count / WORD_BYTES;
        copy_words_down((Word *)to, (const Word *)from, words);
        to -= words * WORD_BYTES;
        from -= words * WORD_BYTES;
        count -= words * WORD_BYTES;
    }
    while (count-- > 0) {
        *--to = *--from;
    }
}

void *memmove(void *destination, const void *source, size_t count)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    if (to <= from || to >= from + count) {
        return memcpy(destination, source, count);
    }
    move_down(to, from, count);
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    uint8_t *to = destination;
    uint8_t byte = (uint8_t)value;
    for (; count > 0 && ((uintptr_t)to & WORD_MASK) != 0; count--) {
        *to++ = byte;
    }
    Word pattern = byte * BYTE_SPREAD;
    Word *words = (Word *)to;
    for (; count >= WORD_BYTES; count -= WORD_BYTES) {
        *words++ = pattern;
    }
    to = (uint8_t *)words;
    while (count-- > 0) {
        *to++ = byte;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = left;
    const uint8_t *b = right;
    for (size_t index = 0; index < count; index++) {
        if (a[index] != b[index]) {
            return a[index] < b[index] ? -1 : 1;
        }
    }
    return 0;
}
