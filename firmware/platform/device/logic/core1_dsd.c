#include "device/dsd_stream.h"

#include "device/core1_source_private.h"
#include "dsd_filter_table.h"

_Static_assert(DSD_RING_BYTES + sizeof(DsdWorkspace) <= 131072,
               "DSD input and workspace must fit core 1 ring RAM");

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void start(void *context, FlacInput input, FlacInfo info, uint32_t first,
                  const bool *starved)
{
    Core1Dsd *dsd = context;
    StreamRegion region = dsd->region;
    (void)info;
    (void)first;
    (void)starved;
    *dsd = (Core1Dsd){.region = region,
                      .work = (DsdWorkspace *)(uintptr_t)(region.ring + DSD_RING_BYTES),
                      .input = input,
                      .generation = region.control->generation,
                      .warmup = 16};
}

static int32_t coefficient(const Core1Dsd *dsd, uint32_t index)
{
    return index < 80 * dsd->ratio - 4 ? dsd->cic[index] : 0;
}

static void tables(Core1Dsd *dsd)
{
    *dsd->work = (DsdWorkspace){0};
    for (uint32_t group = 0; group < dsd->rows; group++) {
        for (uint32_t byte = 0; byte < 256; byte++) {
            int32_t value = 0;
            for (uint32_t bit = 0; bit < 8; bit++) {
                int32_t weight = coefficient(dsd, group * 8 + 7 - bit);
                value += (byte & (1u << bit)) != 0 ? weight : -weight;
            }
            dsd->work->table[group][byte] = value;
        }
    }
}

static Core1DecodeResult configure(Core1Dsd *dsd)
{
    uint8_t header[DSD_HEADER_BYTES];
    if (!dsd->input.read(dsd->input.context, 0, header, sizeof header)) {
        return CORE1_STARVED;
    }
    uint32_t rate = little32(header + 4);
    dsd->channels = little32(header + 8);
    dsd->layout = little32(header + 12);
    dsd->bit_count = little32(header + 16) | (uint64_t)little32(header + 20) << 32;
    dsd->first = little32(header + 24);
    if (little32(header) != 0x50445344u ||
        (rate != 2822400 && rate != 5644800 && rate != 11289600) || dsd->channels < 1 ||
        dsd->channels > 2 || dsd->layout > 2 ||
        little32(header + 28) != (dsd->layout == 0 ? 0u : DSD_BLOCK_BYTES)) {
        return CORE1_FAILED;
    }
    dsd->ratio = rate / 2822400;
    uint32_t step = 32 * dsd->ratio;
    uint64_t frames = dsd->bit_count / step + (dsd->bit_count % step != 0);
    if (frames > UINT32_MAX - dsd->first || dsd->input.size < DSD_HEADER_BYTES) {
        return CORE1_FAILED;
    }
    uint64_t bytes = dsd->bit_count / 8 + (dsd->bit_count % 8 != 0);
    uint64_t needed =
        dsd->layout == 0 ? bytes * dsd->channels : ((bytes + 4095) / 4096) * 4096 * dsd->channels;
    if (needed > dsd->input.size - DSD_HEADER_BYTES) {
        return CORE1_FAILED;
    }
    if (bytes > UINT32_MAX - 512) {
        return CORE1_FAILED;
    }
    dsd->byte_count = (uint32_t)bytes;
    dsd->partial_bits = (uint32_t)(dsd->bit_count % 8);
    dsd->remaining = (uint32_t)frames;
    dsd->shift = dsd->ratio == 1 ? 20u : dsd->ratio == 2 ? 25u : 30u;
    dsd->coefficients = dsd->ratio == 1   ? DSD_SECOND_1
                        : dsd->ratio == 2 ? DSD_SECOND_2
                                          : DSD_SECOND_4;
    dsd->cic = dsd->ratio == 1 ? DSD_CIC_1 : dsd->ratio == 2 ? DSD_CIC_2 : DSD_CIC_4;
    dsd->rows = 10 * dsd->ratio;
    tables(dsd);
    dsd->position = DSD_HEADER_BYTES;
    dsd->configured = true;
    return CORE1_DECODED;
}

static uint8_t reverse(uint8_t value)
{
#if defined(__arm__)
    uint32_t result;
    __asm__("rbit %0, %1" : "=r"(result) : "r"((uint32_t)value));
    return (uint8_t)(result >> 24);
#else
    value = (uint8_t)((value >> 4) | (value << 4));
    value = (uint8_t)(((value & 0xccu) >> 2) | ((value & 0x33u) << 2));
    return (uint8_t)(((value & 0xaau) >> 1) | ((value & 0x55u) << 1));
#endif
}

static Core1DecodeResult packet(Core1Dsd *dsd)
{
    uint32_t bytes = dsd->input.size - dsd->position;
    uint32_t capacity = DSD_BLOCK_BYTES * dsd->channels;
    dsd->packet_size = core1_smaller(bytes, capacity);
    if (dsd->packet_size == 0 || dsd->packet_size % dsd->channels != 0) {
        return CORE1_FAILED;
    }
    if (!dsd->input.read(dsd->input.context, dsd->position, dsd->work->packet, dsd->packet_size)) {
        return CORE1_STARVED;
    }
    if (dsd->region.control->generation != dsd->generation) {
        return CORE1_STARVED;
    }
    dsd->position += dsd->packet_size;
    dsd->packet_frames = dsd->packet_size / dsd->channels;
    dsd->packet_cursor = 0;
    dsd->region.control->released = dsd->region.control->start_offset + dsd->position;
    core1_publish();
    return CORE1_DECODED;
}

static int32_t partial(const Core1Dsd *dsd, uint32_t group, uint8_t byte)
{
    int32_t sum = 0;
    for (uint32_t bit = 0; bit < dsd->partial_bits; bit++) {
        int32_t weight = coefficient(dsd, group * 8 + 7 - bit);
        sum += (byte & (1u << bit)) != 0 ? weight : -weight;
    }
    return sum;
}

static int32_t first_stage(const Core1Dsd *dsd, uint32_t channel)
{
    int32_t sum = 0;
    uint32_t count = core1_smaller(dsd->byte_position, dsd->rows);
    uint32_t skip = dsd->byte_position > dsd->byte_count ? dsd->byte_position - dsd->byte_count : 0;
    const int32_t (*table)[256] = (const int32_t (*)[256])dsd->work->table;
    const uint8_t *history = dsd->work->bits[channel];
    uint32_t at = (dsd->byte_position - 1 - skip) & 63u;
#if defined(__arm__)
#pragma GCC unroll 4
#endif
    for (uint32_t group = skip; group < count; group++) {
        sum += table[group][history[at]];
        at = (at - 1) & 63u;
    }
    if (dsd->partial_bits != 0 && dsd->byte_position >= dsd->byte_count) {
        uint32_t group = dsd->byte_position - dsd->byte_count;
        if (group < count) {
            uint8_t byte = dsd->work->bits[channel][(dsd->byte_count - 1) & 63u];
            sum += partial(dsd, group, byte) - dsd->work->table[group][byte];
        }
    }
    return sum;
}

static void first_stages(const Core1Dsd *dsd, int32_t *values)
{
    if (dsd->channels != 2 || dsd->byte_position < dsd->rows ||
        dsd->byte_position > dsd->byte_count ||
        (dsd->partial_bits != 0 && dsd->byte_position == dsd->byte_count)) {
        values[0] = first_stage(dsd, 0);
        values[1] = dsd->channels == 2 ? first_stage(dsd, 1) : values[0];
        return;
    }
    const int32_t (*table)[256] = (const int32_t (*)[256])dsd->work->table;
    const uint8_t *left = dsd->work->bits[0];
    const uint8_t *right = dsd->work->bits[1];
    uint32_t at = (dsd->byte_position - 1) & 63u;
    int32_t a = 0;
    int32_t b = 0;
#if defined(__arm__)
#pragma GCC unroll 4
#endif
    for (uint32_t group = 0; group < dsd->rows; group++) {
        a += table[group][left[at]];
        b += table[group][right[at]];
        at = (at - 1) & 63u;
    }
    values[0] = a;
    values[1] = b;
}

static int32_t second_stage(const Core1Dsd *dsd, uint32_t channel)
{
    const int32_t *history = dsd->work->history[channel] + dsd->stage_position;
    int64_t sum = 0;
#if defined(__arm__)
#pragma GCC unroll 4
#endif
    for (uint32_t index = 0; index < 32; index++) {
        sum += (int64_t)dsd->coefficients[index] * (history[index] + history[63 - index]);
    }
    int64_t value = (sum + ((int64_t)1 << 33)) >> 34;
    return value < -8388608 ? -8388608 : value > 8388607 ? 8388607 : (int32_t)value;
}

static Core1DecodeResult advance(Core1Dsd *dsd)
{
    uint32_t step = 2 * dsd->ratio;
    uint32_t available = dsd->byte_position < dsd->byte_count
                             ? core1_smaller(step, dsd->byte_count - dsd->byte_position)
                             : 0;
    if (available != 0) {
        if (dsd->packet_cursor == dsd->packet_frames) {
            Core1DecodeResult result = packet(dsd);
            if (result != CORE1_DECODED) {
                return result;
            }
        }
        if (available > dsd->packet_frames - dsd->packet_cursor) {
            return CORE1_FAILED;
        }
        for (uint32_t channel = 0; channel < dsd->channels; channel++) {
            uint32_t base = dsd->layout == 0 ? dsd->packet_cursor * dsd->channels + channel
                                             : channel * DSD_BLOCK_BYTES + dsd->packet_cursor;
            uint32_t stride = dsd->layout == 0 ? dsd->channels : 1;
            const uint8_t *raw = dsd->work->packet + base;
            uint8_t *history = dsd->work->bits[channel];
            for (uint32_t at = 0; at < available; at++) {
                uint8_t value = *raw;
                history[(dsd->byte_position + at) & 63u] =
                    dsd->layout == 1 ? value : reverse(value);
                raw += stride;
            }
        }
        dsd->packet_cursor += available;
    }
    dsd->byte_position += step;
    int32_t values[2];
    first_stages(dsd, values);
    for (uint32_t channel = 0; channel < dsd->channels; channel++) {
        int32_t value = dsd->shift < 28 ? (int32_t)((uint32_t)values[channel] << (28 - dsd->shift))
                                        : values[channel] >> (dsd->shift - 28);
        dsd->work->history[channel][dsd->stage_position] = value;
        dsd->work->history[channel][dsd->stage_position + DSD_SECOND_TAPS] = value;
    }
    dsd->stage_position++;
    if (dsd->stage_position == DSD_SECOND_TAPS) {
        dsd->stage_position = 0;
    }
    if (++dsd->stage_count % 2 != 0) {
        return CORE1_DECODED;
    }
    if (dsd->warmup != 0) {
        dsd->warmup--;
        return CORE1_DECODED;
    }
    for (uint32_t channel = 0; channel < dsd->channels; channel++) {
        dsd->region.samples[dsd->slot][channel][dsd->staged] = second_stage(dsd, channel);
    }
    dsd->remaining--;
    dsd->staged++;
    return CORE1_DECODED;
}

static Core1DecodeResult decode(void *context, Core1Block *block)
{
    Core1Dsd *dsd = context;
    if (!dsd->configured) {
        Core1DecodeResult result = configure(dsd);
        if (result != CORE1_DECODED) {
            return result;
        }
    }
    while (dsd->remaining != 0 && dsd->staged < DSD_OUTPUT_FRAMES) {
        Core1DecodeResult result = advance(dsd);
        if (result != CORE1_DECODED) {
            return result;
        }
    }
    if (dsd->staged == 0) {
        return CORE1_FINISHED;
    }
    *block = (Core1Block){.channels = {dsd->region.samples[dsd->slot][0],
                                       dsd->region.samples[dsd->slot][dsd->channels > 1 ? 1 : 0]},
                          .first_sample = dsd->first,
                          .samples = dsd->staged,
                          .position = dsd->position};
    dsd->first += dsd->staged;
    dsd->staged = 0;
    dsd->slot ^= 1u;
    return CORE1_DECODED;
}

Core1Decoder core1_dsd_decoder(Core1Dsd *dsd, StreamRegion region)
{
    *dsd = (Core1Dsd){.region = region};
    return (Core1Decoder){start, decode, dsd};
}
