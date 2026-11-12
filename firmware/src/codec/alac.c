#include "codec/alac.h"

#include <stddef.h>

enum {
    ALAC_TAG_BITS = 3,
    ALAC_TAG_SCE = 0,
    ALAC_TAG_CPE = 1,
    ALAC_TAG_END = 7,
    ALAC_HEADER_SKIP_BITS = 16,
    ALAC_FLAG_PARTIAL = 8,
    ALAC_FLAG_ESCAPE = 1,
    ALAC_SHIFT_BYTES_MAX = 2,
    ALAC_SHIFT_BYTE_BITS = 8,
    ALAC_MAX_ORDER = 32,
    ALAC_ORDER_SIMPLE = 31,
    ALAC_COEFFICIENT_BITS = 16,
    ALAC_PREFIX_MAX = 9,
    ALAC_RUN_BITS = 16,
    ALAC_RUN_LIMIT = 65535,
    ALAC_MEAN_SHIFT = 9,
    ALAC_MEAN_RUN = 128,
    ALAC_MEAN_CLAMP = 0xFFFF,
    ALAC_RUN_BIAS = 24,
    ALAC_RUN_ROUND = 16,
    ALAC_RUN_SCALE = 6,
    ALAC_REFILL_LEVEL = 24,
    ALAC_MAX_PACKET_BYTES = 1 << 24,
    ALAC_PB_FACTOR_DIVISOR = 4
};

typedef struct AlacBits {
    const uint8_t *data;
    uint32_t length;
    uint32_t next;
    uint32_t word;
    uint32_t fill;
} AlacBits;

typedef struct AlacRice {
    uint32_t multiplier;
    uint32_t k;
    uint32_t escape_bits;
} AlacRice;

typedef struct AlacGolomb {
    uint32_t pb;
    uint32_t kb;
    uint32_t mask;
    uint32_t sample_bits;
    uint32_t mean;
} AlacGolomb;

typedef struct AlacResidual {
    int32_t *out;
    uint32_t count;
    uint32_t done;
    uint32_t mean;
    uint32_t zmode;
} AlacResidual;

typedef struct AlacLpc {
    int16_t coefs[ALAC_MAX_ORDER];
    uint8_t mode;
    uint8_t shift;
    uint8_t factor;
    uint8_t order;
} AlacLpc;

typedef struct AlacElement {
    AlacLpc lpc[ALAC_MAX_CHANNELS];
    uint32_t channels;
    uint32_t count;
    uint32_t shift_bytes;
    uint32_t sample_bits;
    uint32_t mix_bits;
    int32_t mix_res;
    uint32_t shift_position;
    bool escape;
} AlacElement;

static uint32_t read_be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

bool alac_parse_cookie(const uint8_t *bytes, uint32_t length, AlacConfig *config)
{
    if (bytes == NULL || config == NULL || length < ALAC_COOKIE_BYTES || bytes[4] != 0) {
        return false;
    }
    AlacConfig parsed = {
        .frame_length = read_be32(bytes),
        .bit_depth = bytes[5],
        .pb = bytes[6],
        .mb = bytes[7],
        .kb = bytes[8],
        .channels = bytes[9],
        .max_run = (uint16_t)((bytes[10] << 8) | bytes[11]),
        .sample_rate = read_be32(bytes + 20),
    };
    if (!alac_config_supported(&parsed)) {
        return false;
    }
    *config = parsed;
    return true;
}

void alac_start(AlacDecoder *decoder, const AlacConfig *config)
{
    decoder->config = *config;
    decoder->sample_count = 0;
}

uint32_t alac_samples(const AlacDecoder *decoder)
{
    return decoder->sample_count;
}

const int32_t *alac_channel(const AlacDecoder *decoder, int channel)
{
    bool second = channel != 0 && decoder->config.channels == 2;
    return decoder->samples[second ? 1 : 0];
}

static void bits_start(AlacBits *bits, const uint8_t *data, uint32_t length)
{
    *bits = (AlacBits){.data = data, .length = length};
}

static void bits_refill(AlacBits *bits)
{
    while (bits->fill <= ALAC_REFILL_LEVEL) {
        uint32_t byte = bits->next < bits->length ? bits->data[bits->next] : 0u;
        bits->next++;
        bits->word |= byte << (ALAC_REFILL_LEVEL - bits->fill);
        bits->fill += 8;
    }
}

static void bits_drop(AlacBits *bits, uint32_t count)
{
    bits->word <<= count;
    bits->fill -= count;
}

static uint32_t bits_read(AlacBits *bits, uint32_t count)
{
    if (count == 0) {
        return 0;
    }
    bits_refill(bits);
    uint32_t value = bits->word >> (32 - count);
    bits_drop(bits, count);
    return value;
}

static uint32_t bits_position(const AlacBits *bits)
{
    return bits->next * 8u - bits->fill;
}

static bool bits_overrun(const AlacBits *bits)
{
    return bits_position(bits) > bits->length * 8u;
}

static void bits_seek(AlacBits *bits, uint32_t position)
{
    bits->word = 0;
    bits->fill = 0;
    if (position > bits->length * 8u) {
        bits->next = bits->length + 1;
        return;
    }
    bits->next = position >> 3;
    (void)bits_read(bits, position & 7u);
}

static uint32_t rice_read(AlacBits *bits, AlacRice rice)
{
    bits_refill(bits);
    uint32_t prefix = (uint32_t)__builtin_clz(~bits->word | 1u);
    if (prefix >= ALAC_PREFIX_MAX) {
        bits_drop(bits, ALAC_PREFIX_MAX);
        return bits_read(bits, rice.escape_bits);
    }
    bits_drop(bits, prefix + 1);
    uint32_t value = prefix * rice.multiplier;
    if (rice.k < 2) {
        return value;
    }
    bits_refill(bits);
    uint32_t extra = bits->word >> (32 - rice.k);
    if (extra > 1) {
        value += extra - 1;
        bits_drop(bits, rice.k);
    } else {
        bits_drop(bits, rice.k - 1);
    }
    return value;
}

static uint32_t count_leading_zeros(uint32_t value)
{
    return value == 0 ? 32u : (uint32_t)__builtin_clz(value);
}

static int32_t unfold_sign(uint32_t value)
{
    uint32_t magnitude = (value + 1) >> 1;
    return (int32_t)((value & 1u) != 0 ? 0u - magnitude : magnitude);
}

static void residual_sample(AlacBits *bits, AlacResidual *state, const AlacGolomb *golomb)
{
    uint32_t k = 31u - count_leading_zeros((state->mean >> ALAC_MEAN_SHIFT) + 3u);
    k = k < golomb->kb ? k : golomb->kb;
    uint32_t n = rice_read(bits, (AlacRice){(1u << k) - 1u, k, golomb->sample_bits});
    uint32_t folded = n + state->zmode;
    state->out[state->done++] = unfold_sign(folded);
    uint32_t mean =
        golomb->pb * folded + state->mean - ((golomb->pb * state->mean) >> ALAC_MEAN_SHIFT);
    state->mean = n > ALAC_MEAN_CLAMP ? ALAC_MEAN_CLAMP : mean;
    state->zmode = 0;
}

static bool residual_run(AlacBits *bits, AlacResidual *state, const AlacGolomb *golomb)
{
    uint32_t mean = state->mean;
    uint32_t k =
        count_leading_zeros(mean) - ALAC_RUN_BIAS + ((mean + ALAC_RUN_ROUND) >> ALAC_RUN_SCALE);
    uint32_t multiplier = ((1u << k) - 1u) & golomb->mask;
    uint32_t run = rice_read(bits, (AlacRice){multiplier, k, ALAC_RUN_BITS});
    if (run > state->count - state->done) {
        return false;
    }
    for (uint32_t index = 0; index < run; index++) {
        state->out[state->done + index] = 0;
    }
    state->done += run;
    state->zmode = run < ALAC_RUN_LIMIT ? 1u : 0u;
    state->mean = 0;
    return true;
}

static bool read_residuals(AlacBits *bits, int32_t *out, uint32_t count, const AlacGolomb *golomb)
{
    AlacResidual state = {.out = out, .count = count, .mean = golomb->mean};
    while (state.done < state.count) {
        residual_sample(bits, &state, golomb);
        if (state.mean < ALAC_MEAN_RUN && state.done < state.count) {
            state.zmode = 1;
            if (!residual_run(bits, &state, golomb)) {
                return false;
            }
        }
    }
    return true;
}

static int32_t extend_sign(uint32_t value, uint32_t shift)
{
    return (int32_t)(value << shift) >> shift;
}

static int32_t sign_of(int32_t value)
{
    return (value > 0) - (value < 0);
}

static void lpc_simple(int32_t *samples, uint32_t count, uint32_t shift)
{
    for (uint32_t index = 1; index < count; index++) {
        samples[index] =
            extend_sign((uint32_t)samples[index - 1] + (uint32_t)samples[index], shift);
    }
}

typedef struct AlacRun {
    int32_t *samples;
    uint32_t count;
    uint32_t shift;
} AlacRun;

static inline __attribute__((always_inline)) uint32_t lpc_sum(const int32_t *taps, int32_t anchor,
                                                              const int32_t *coefs, uint32_t order)
{
    uint32_t sum = 0;
#pragma GCC unroll 32
    for (uint32_t tap = 0; tap < order; tap++) {
        sum += ((uint32_t)taps[tap] - (uint32_t)anchor) * (uint32_t)coefs[tap];
    }
    return sum;
}

static inline __attribute__((always_inline)) void lpc_adapt(int32_t *coefs, const int32_t *taps,
                                                            int32_t error, const AlacLpc *lpc)
{
    int32_t anchor = taps[-1];
    int32_t direction = sign_of(error);
#pragma GCC unroll 32
    for (uint32_t tap = 0; tap < lpc->order && error * direction > 0; tap++) {
        int32_t difference = anchor - taps[tap];
        int32_t sign = sign_of(difference) * direction;
        coefs[tap] = (int16_t)(coefs[tap] - sign);
        error -= ((difference * sign) >> lpc->shift) * (int32_t)(tap + 1);
    }
}

static inline __attribute__((always_inline)) void lpc_filter_order(AlacRun run, AlacLpc *lpc,
                                                                   uint32_t order)
{
    int32_t coefs[ALAC_MAX_ORDER];
    AlacLpc fixed = *lpc;
    fixed.order = (uint8_t)order;
    for (uint32_t tap = 0; tap < order; tap++) {
        coefs[tap] = lpc->coefs[tap];
    }
    uint32_t round = lpc->shift != 0 ? 1u << (lpc->shift - 1u) : 0u;
    for (uint32_t index = order + 1; index < run.count; index++) {
        const int32_t *taps = run.samples + index - order;
        int32_t anchor = taps[-1];
        int32_t error = run.samples[index];
        int32_t predicted = (int32_t)(lpc_sum(taps, anchor, coefs, order) + round) >> lpc->shift;
        run.samples[index] =
            extend_sign((uint32_t)predicted + (uint32_t)anchor + (uint32_t)error, run.shift);
        if (error != 0) {
            lpc_adapt(coefs, taps, error, &fixed);
        }
    }
    for (uint32_t tap = 0; tap < order; tap++) {
        lpc->coefs[tap] = (int16_t)coefs[tap];
    }
}

static void lpc_filter_4(AlacRun run, AlacLpc *lpc)
{
    lpc_filter_order(run, lpc, 4);
}

static void lpc_filter_8(AlacRun run, AlacLpc *lpc)
{
    lpc_filter_order(run, lpc, 8);
}

static void lpc_filter_any(AlacRun run, AlacLpc *lpc)
{
    lpc_filter_order(run, lpc, lpc->order);
}

static void lpc_filter(int32_t *samples, uint32_t count, AlacLpc *lpc, uint32_t shift)
{
    AlacRun run = {.samples = samples, .count = count, .shift = shift};
    if (lpc->order == 4) {
        lpc_filter_4(run, lpc);
    } else if (lpc->order == 8) {
        lpc_filter_8(run, lpc);
    } else {
        lpc_filter_any(run, lpc);
    }
}

static void unfix(int32_t *samples, uint32_t count, AlacLpc *lpc, uint32_t sample_bits)
{
    uint32_t shift = 32u - sample_bits;
    if (lpc->mode != 0) {
        lpc_simple(samples, count, shift);
    }
    if (lpc->order == ALAC_ORDER_SIMPLE) {
        lpc_simple(samples, count, shift);
    } else if (lpc->order > 0) {
        uint32_t warm = lpc->order + 1u < count ? lpc->order + 1u : count;
        lpc_simple(samples, warm, shift);
        lpc_filter(samples, count, lpc, shift);
    }
}

static void read_lpc(AlacBits *bits, AlacLpc *lpc)
{
    lpc->mode = (uint8_t)bits_read(bits, 4);
    lpc->shift = (uint8_t)bits_read(bits, 4);
    lpc->factor = (uint8_t)bits_read(bits, 3);
    lpc->order = (uint8_t)bits_read(bits, 5);
    for (uint32_t tap = 0; tap < lpc->order; tap++) {
        uint32_t slot = lpc->order - 1u - tap;
        lpc->coefs[slot] = (int16_t)extend_sign(bits_read(bits, ALAC_COEFFICIENT_BITS), 16);
    }
}

static AlacResult read_header(const AlacDecoder *decoder, AlacBits *bits, AlacElement *element)
{
    const AlacConfig *config = &decoder->config;
    (void)bits_read(bits, ALAC_HEADER_SKIP_BITS);
    uint32_t flags = bits_read(bits, 4);
    element->escape = (flags & ALAC_FLAG_ESCAPE) != 0;
    element->shift_bytes = (flags >> 1) & 3u;
    element->count = config->frame_length;
    if ((flags & ALAC_FLAG_PARTIAL) != 0) {
        element->count = bits_read(bits, 16) << 16;
        element->count |= bits_read(bits, 16);
    }
    uint32_t shifted = element->shift_bytes * ALAC_SHIFT_BYTE_BITS;
    if (element->count == 0 || element->count > config->frame_length ||
        element->shift_bytes > ALAC_SHIFT_BYTES_MAX || shifted >= config->bit_depth) {
        return ALAC_BAD_DATA;
    }
    element->sample_bits = config->bit_depth - shifted + (element->channels - 1u);
    return ALAC_OK;
}

static void read_raw(AlacDecoder *decoder, AlacBits *bits, const AlacElement *element)
{
    uint32_t depth = decoder->config.bit_depth;
    for (uint32_t index = 0; index < element->count; index++) {
        for (uint32_t channel = 0; channel < element->channels; channel++) {
            decoder->samples[channel][index] = extend_sign(bits_read(bits, depth), 32u - depth);
        }
    }
}

static AlacResult read_channel(AlacDecoder *decoder, AlacBits *bits, AlacElement *element,
                               uint32_t channel)
{
    const AlacConfig *config = &decoder->config;
    AlacLpc *lpc = &element->lpc[channel];
    AlacGolomb golomb = {
        .pb = (uint32_t)config->pb * lpc->factor / ALAC_PB_FACTOR_DIVISOR,
        .kb = config->kb,
        .mask = (1u << config->kb) - 1u,
        .sample_bits = element->sample_bits,
        .mean = config->mb,
    };
    int32_t *samples = decoder->samples[channel];
    if (!read_residuals(bits, samples, element->count, &golomb)) {
        return ALAC_BAD_DATA;
    }
    unfix(samples, element->count, lpc, element->sample_bits);
    return ALAC_OK;
}

static void unmix(AlacDecoder *decoder, const AlacElement *element)
{
    int32_t *first = decoder->samples[0];
    int32_t *second = decoder->samples[1];
    uint32_t weight = (uint32_t)element->mix_res;
    for (uint32_t index = 0; index < element->count; index++) {
        uint32_t side = (uint32_t)second[index];
        uint32_t scaled = (uint32_t)((int32_t)(weight * side) >> element->mix_bits);
        uint32_t left = (uint32_t)first[index] + side - scaled;
        first[index] = (int32_t)left;
        second[index] = (int32_t)(left - side);
    }
}

static void merge_shifted(AlacDecoder *decoder, const AlacBits *bits, const AlacElement *element)
{
    AlacBits extra = *bits;
    bits_seek(&extra, element->shift_position);
    uint32_t width = element->shift_bytes * ALAC_SHIFT_BYTE_BITS;
    for (uint32_t index = 0; index < element->count; index++) {
        for (uint32_t channel = 0; channel < element->channels; channel++) {
            uint32_t high = (uint32_t)decoder->samples[channel][index] << width;
            decoder->samples[channel][index] = (int32_t)(high | bits_read(&extra, width));
        }
    }
}

static AlacResult finish_element(AlacDecoder *decoder, const AlacBits *bits,
                                 const AlacElement *element)
{
    if (element->channels == 2 && element->mix_res != 0) {
        if (element->mix_bits > 31) {
            return ALAC_BAD_DATA;
        }
        unmix(decoder, element);
    }
    if (element->shift_bytes != 0) {
        merge_shifted(decoder, bits, element);
    }
    return ALAC_OK;
}

static AlacResult read_compressed(AlacDecoder *decoder, AlacBits *bits, AlacElement *element)
{
    element->mix_bits = bits_read(bits, 8);
    element->mix_res = extend_sign(bits_read(bits, 8), 24);
    for (uint32_t channel = 0; channel < element->channels; channel++) {
        read_lpc(bits, &element->lpc[channel]);
    }
    element->shift_position = bits_position(bits);
    uint32_t skipped =
        element->count * element->channels * element->shift_bytes * ALAC_SHIFT_BYTE_BITS;
    bits_seek(bits, element->shift_position + skipped);
    for (uint32_t channel = 0; channel < element->channels; channel++) {
        AlacResult result = read_channel(decoder, bits, element, channel);
        if (result != ALAC_OK) {
            return result;
        }
    }
    return finish_element(decoder, bits, element);
}

static AlacResult read_element(AlacDecoder *decoder, AlacBits *bits, uint32_t channels)
{
    AlacElement element;
    element.channels = channels;
    element.mix_bits = 0;
    element.mix_res = 0;
    element.shift_position = 0;
    AlacResult result = read_header(decoder, bits, &element);
    if (result != ALAC_OK) {
        return result;
    }
    if (element.escape) {
        read_raw(decoder, bits, &element);
    } else {
        result = read_compressed(decoder, bits, &element);
    }
    if (result == ALAC_OK) {
        decoder->sample_count = bits_overrun(bits) ? 0 : element.count;
        result = bits_overrun(bits) ? ALAC_BAD_DATA : ALAC_OK;
    }
    return result;
}

AlacResult alac_decode_packet(AlacDecoder *decoder, const uint8_t *packet, uint32_t length)
{
    decoder->sample_count = 0;
    if (length == 0) {
        return ALAC_END;
    }
    if (packet == NULL || length > ALAC_MAX_PACKET_BYTES) {
        return ALAC_BAD_DATA;
    }
    AlacBits bits;
    bits_start(&bits, packet, length);
    uint32_t tag = bits_read(&bits, ALAC_TAG_BITS);
    if (tag == ALAC_TAG_END) {
        return ALAC_END;
    }
    uint32_t channels = tag + 1u;
    if (tag > ALAC_TAG_CPE || channels != decoder->config.channels) {
        return ALAC_UNSUPPORTED;
    }
    return read_element(decoder, &bits, channels);
}
