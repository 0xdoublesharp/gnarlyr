#include "entenc.h"

static _Noreturn void encoder_absent(void)
{
    celt_fatal("", "", 0);
}

void ec_encode(ec_enc *encoder, unsigned low, unsigned high, unsigned total)
{
    (void)encoder;
    (void)low;
    (void)high;
    (void)total;
    encoder_absent();
}

void ec_enc_uint(ec_enc *encoder, opus_uint32 value, opus_uint32 total)
{
    (void)encoder;
    (void)value;
    (void)total;
    encoder_absent();
}

void ec_enc_bits(ec_enc *encoder, opus_uint32 value, unsigned bits)
{
    (void)encoder;
    (void)value;
    (void)bits;
    encoder_absent();
}
