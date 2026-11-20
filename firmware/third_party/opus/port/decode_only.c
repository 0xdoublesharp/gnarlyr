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

void ec_encode_bin(ec_enc *encoder, unsigned low, unsigned high, unsigned bits)
{
    (void)encoder;
    (void)low;
    (void)high;
    (void)bits;
    encoder_absent();
}

void ec_enc_bit_logp(ec_enc *encoder, int value, unsigned logp)
{
    (void)encoder;
    (void)value;
    (void)logp;
    encoder_absent();
}

void ec_enc_icdf(ec_enc *encoder, int symbol, const unsigned char *icdf, unsigned bits)
{
    (void)encoder;
    (void)symbol;
    (void)icdf;
    (void)bits;
    encoder_absent();
}

void ec_enc_icdf16(ec_enc *encoder, int symbol, const opus_uint16 *icdf, unsigned bits)
{
    (void)encoder;
    (void)symbol;
    (void)icdf;
    (void)bits;
    encoder_absent();
}
