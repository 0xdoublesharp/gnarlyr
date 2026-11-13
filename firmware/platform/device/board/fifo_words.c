#include "fifo_words.h"

enum {
    FIFO_WORDS_UNROLL = 8
};

void fifo_words_read(uint32_t *to, uint32_t fifo, uint32_t count)
{
    volatile const uint32_t *port = (volatile const uint32_t *)fifo;
    for (; count >= FIFO_WORDS_UNROLL; count -= FIFO_WORDS_UNROLL, to += FIFO_WORDS_UNROLL) {
        to[0] = *port;
        to[1] = *port;
        to[2] = *port;
        to[3] = *port;
        to[4] = *port;
        to[5] = *port;
        to[6] = *port;
        to[7] = *port;
    }
    for (; count > 0; count--) {
        *to++ = *port;
    }
}

void fifo_words_write(uint32_t fifo, const uint32_t *from, uint32_t count)
{
    volatile uint32_t *port = (volatile uint32_t *)fifo;
    for (; count >= FIFO_WORDS_UNROLL; count -= FIFO_WORDS_UNROLL, from += FIFO_WORDS_UNROLL) {
        *port = from[0];
        *port = from[1];
        *port = from[2];
        *port = from[3];
        *port = from[4];
        *port = from[5];
        *port = from[6];
        *port = from[7];
    }
    for (; count > 0; count--) {
        *port = *from++;
    }
}
