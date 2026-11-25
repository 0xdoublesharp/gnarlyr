#include "device/dsd_stream.h"

#include "core/bytes.h"

#include "device/core1_source_private.h"
#include "dsd_filter_table.h"

_Static_assert(DSD_RING_BYTES + sizeof(DsdWorkspace) <= 131072,
               "DSD input and workspace must fit core 1 ring RAM");
_Static_assert((int)DSD_OUTPUT_FRAMES <= (int)FLAC_MAX_BLOCK,
               "a DSD output block must fit a core 1 slot");

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

static uint8_t ordered(const Core1Dsd *dsd, uint8_t raw)
{
    return dsd->layout == 1 ? raw : reverse(raw);
}

static int32_t weighted(const Core1Dsd *dsd, uint32_t group, uint8_t bits, uint32_t count)
{
    int32_t sum = 0;
    for (uint32_t bit = 0; bit < count; bit++) {
        int32_t weight = coefficient(dsd, group * 8 + 7 - bit);
        sum += (bits & (1u << bit)) != 0 ? weight : -weight;
    }
    return sum;
}

static void tables(Core1Dsd *dsd)
{
    *dsd->work = (DsdWorkspace){0};
    for (uint32_t group = 0; group < dsd->rows; group++) {
        for (uint32_t byte = 0; byte < 256; byte++) {
            dsd->work->table[group][byte] = weighted(dsd, group, ordered(dsd, (uint8_t)byte), 8);
        }
    }
}

static Core1DecodeResult configure(Core1Dsd *dsd)
{
    uint8_t header[DSD_HEADER_BYTES];
    if (!dsd->input.read(dsd->input.context, 0, header, sizeof header)) {
        return CORE1_STARVED;
    }
    uint32_t rate = bytes_le32(header + 4);
    dsd->channels = bytes_le32(header + 8);
    dsd->layout = bytes_le32(header + 12);
    dsd->bit_count = bytes_le32(header + 16) | (uint64_t)bytes_le32(header + 20) << 32;
    dsd->first = bytes_le32(header + 24);
    uint32_t size_flags = bytes_le32(header + 28);
    dsd->dop = (size_flags & DSD_HEADER_DOP) != 0;
    if (bytes_le32(header) != 0x50445344u ||
        (rate != DSD64_RATE && rate != 5644800 && rate != 11289600) ||
        (dsd->dop && rate != DSD64_RATE) || dsd->channels < 1 || dsd->channels > 2 ||
        dsd->layout > 2 ||
        (size_flags & ~DSD_HEADER_DOP) != (dsd->layout == 0 ? 0u : DSD_BLOCK_BYTES)) {
        return CORE1_FAILED;
    }
    dsd->ratio = rate / DSD64_RATE;
    uint32_t step = dsd->dop ? 16 : 32 * dsd->ratio;
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
    dsd->steady_end = dsd->byte_count - (dsd->partial_bits != 0);
    dsd->remaining = (uint32_t)frames;
    dsd->position = DSD_HEADER_BYTES;
    dsd->lane_start = 0u - DSD_LANE_HISTORY;
    dsd->configured = true;
    if (dsd->dop) {
        core1_stream_clock(dsd->region, 50);
        return CORE1_DECODED;
    }
    dsd->raise = dsd->ratio == 1 ? 8u : dsd->ratio == 2 ? 3u : 0u;
    dsd->lower = dsd->ratio == 4 ? 2u : 0u;
    dsd->coefficients = dsd->ratio == 1   ? DSD_SECOND_1
                        : dsd->ratio == 2 ? DSD_SECOND_2
                                          : DSD_SECOND_4;
    dsd->cic = dsd->ratio == 1 ? DSD_CIC_1 : dsd->ratio == 2 ? DSD_CIC_2 : DSD_CIC_4;
    dsd->rows = 10 * dsd->ratio;
    core1_stream_clock(dsd->region, dsd->ratio == 1 && dsd->channels == 1 ? 100 : 200);
    tables(dsd);
    return CORE1_DECODED;
}

static const uint8_t *lane(const Core1Dsd *dsd, uint32_t channel)
{
    return dsd->work->lanes[dsd->lane][channel];
}

static void deinterleave(DsdWorkspace *work, uint32_t next, uint32_t frames)
{
    uint8_t *left = work->lanes[next][0] + DSD_LANE_HISTORY;
    uint8_t *right = work->lanes[next][1] + DSD_LANE_HISTORY;
    const uint8_t *from = work->packet;
    for (uint32_t at = 0; at < frames; at++) {
        left[at] = from[2 * at];
        right[at] = from[2 * at + 1];
    }
}

static bool load(Core1Dsd *dsd, uint32_t next, uint32_t size)
{
    FlacInput input = dsd->input;
    if (dsd->layout == 0 && dsd->channels == 2) {
        if (!input.read(input.context, dsd->position, dsd->work->packet, size)) {
            return false;
        }
        deinterleave(dsd->work, next, size / 2);
        return true;
    }
    for (uint32_t channel = 0; channel < dsd->channels; channel++) {
        uint32_t offset = channel * DSD_BLOCK_BYTES;
        uint32_t count = offset < size ? core1_smaller(size - offset, DSD_BLOCK_BYTES) : 0;
        uint8_t *to = dsd->work->lanes[next][channel] + DSD_LANE_HISTORY;
        if (count != 0 && !input.read(input.context, dsd->position + offset, to, count)) {
            return false;
        }
    }
    return true;
}

static void carry(Core1Dsd *dsd, uint32_t next)
{
    for (uint32_t channel = 0; channel < dsd->channels; channel++) {
        const uint8_t *from = lane(dsd, channel) + dsd->packet_frames;
        uint8_t *to = dsd->work->lanes[next][channel];
        for (uint32_t at = 0; at < DSD_LANE_HISTORY; at++) {
            to[at] = from[at];
        }
    }
}

static Core1DecodeResult packet(Core1Dsd *dsd)
{
    uint32_t bytes = dsd->input.size - dsd->position;
    uint32_t size = core1_smaller(bytes, DSD_BLOCK_BYTES * dsd->channels);
    if (dsd->channels == 0 || size == 0 || size % dsd->channels != 0) {
        return CORE1_FAILED;
    }
    uint32_t next = dsd->lane ^ 1u;
    if (!load(dsd, next, size) || dsd->region.control->generation != dsd->generation) {
        return CORE1_STARVED;
    }
    if (!dsd->dop) {
        carry(dsd, next);
    }
    dsd->lane = next;
    dsd->lane_start = dsd->byte_position - DSD_LANE_HISTORY;
    dsd->position += size;
    dsd->packet_frames = size / dsd->channels;
    dsd->packet_cursor = 0;
    dsd->region.control->released = dsd->region.control->start_offset + dsd->position;
    core1_publish();
    return CORE1_DECODED;
}

static int32_t first_stage(const Core1Dsd *dsd, uint32_t channel)
{
    int32_t sum = 0;
    uint32_t count = core1_smaller(dsd->byte_position, dsd->rows);
    uint32_t skip = dsd->byte_position > dsd->byte_count ? dsd->byte_position - dsd->byte_count : 0;
    const uint8_t *bytes = lane(dsd, channel);
    uint32_t at = dsd->byte_position - 1 - skip - dsd->lane_start;
    for (uint32_t group = skip; group < count; group++) {
        sum += dsd->work->table[group][bytes[at--]];
    }
    if (dsd->partial_bits != 0 && dsd->byte_position >= dsd->byte_count) {
        uint32_t group = dsd->byte_position - dsd->byte_count;
        if (group < count) {
            uint8_t raw = bytes[dsd->byte_count - 1 - dsd->lane_start];
            sum += weighted(dsd, group, ordered(dsd, raw), dsd->partial_bits) -
                   dsd->work->table[group][raw];
        }
    }
    return sum;
}

static bool steady(const Core1Dsd *dsd)
{
    return dsd->byte_position >= dsd->rows && dsd->byte_position <= dsd->steady_end;
}

static void first_pair(const Core1Dsd *dsd, int32_t *values)
{
    uint32_t newest = dsd->byte_position - 1 - dsd->lane_start;
    const uint8_t *left = lane(dsd, 0) + newest;
    const uint8_t *right = lane(dsd, 1) + newest;
    int32_t (*row)[256] = dsd->work->table;
    int32_t a = 0;
    int32_t b = 0;
#if defined(__arm__)
#pragma GCC unroll 10
#endif
    for (uint32_t group = 0; group < dsd->rows; group++) {
        a += (*row)[*left--];
        b += (*row)[*right--];
        row++;
    }
    values[0] = a;
    values[1] = b;
}

static int32_t first_one(const Core1Dsd *dsd)
{
    const uint8_t *bytes = lane(dsd, 0) + (dsd->byte_position - 1 - dsd->lane_start);
    int32_t (*row)[256] = dsd->work->table;
    int32_t sum = 0;
#if defined(__arm__)
#pragma GCC unroll 10
#endif
    for (uint32_t group = 0; group < dsd->rows; group++) {
        sum += (*row)[*bytes--];
        row++;
    }
    return sum;
}

static void first_stages(const Core1Dsd *dsd, int32_t *values)
{
    if (!steady(dsd)) {
        values[0] = first_stage(dsd, 0);
        values[1] = dsd->channels == 2 ? first_stage(dsd, 1) : values[0];
    } else if (dsd->channels == 2) {
        first_pair(dsd, values);
    } else {
        values[0] = first_one(dsd);
        values[1] = values[0];
    }
}

static int32_t rounded(int64_t sum)
{
    int64_t value = (sum + ((int64_t)1 << 33)) >> 34;
    return value < -8388608 ? -8388608 : value > 8388607 ? 8388607 : (int32_t)value;
}

__attribute__((noinline)) static void
second_pair(const int32_t (*low)[2], const int32_t *coefficients, int32_t *left, int32_t *right)
{
    const int32_t (*high)[2] = low + DSD_SECOND_TAPS - 1;
    int64_t a = 0;
    int64_t b = 0;
#if defined(__arm__)
#pragma GCC unroll 32
#endif
    for (uint32_t index = 0; index < DSD_SECOND_TAPS / 2; index++) {
        int64_t weight = coefficients[index];
        a += weight * (low[index][0] + high[-(int32_t)index][0]);
        b += weight * (low[index][1] + high[-(int32_t)index][1]);
    }
    *left = rounded(a);
    *right = rounded(b);
}

__attribute__((noinline)) static int32_t second_one(const int32_t (*low)[2],
                                                    const int32_t *coefficients)
{
    const int32_t (*high)[2] = low + DSD_SECOND_TAPS - 1;
    int64_t sum = 0;
#if defined(__arm__)
#pragma GCC unroll 32
#endif
    for (uint32_t index = 0; index < DSD_SECOND_TAPS / 2; index++) {
        sum += (int64_t)coefficients[index] * (low[index][0] + high[-(int32_t)index][0]);
    }
    return rounded(sum);
}

static void second_stage(Core1Dsd *dsd)
{
    int32_t (*samples)[FLAC_MAX_BLOCK] = dsd->region.samples[dsd->slot];
    const int32_t (*low)[2] = (const int32_t (*)[2])(dsd->work->history + dsd->stage_position);
    if (dsd->channels == 2) {
        second_pair(low, dsd->coefficients, &samples[0][dsd->staged], &samples[1][dsd->staged]);
    } else {
        samples[0][dsd->staged] = second_one(low, dsd->coefficients);
    }
}

static Core1DecodeResult take(Core1Dsd *dsd)
{
    uint32_t step = 2 * dsd->ratio;
    if (dsd->packet_cursor + step <= dsd->packet_frames &&
        dsd->byte_count - dsd->byte_position >= step) {
        dsd->packet_cursor += step;
        return CORE1_DECODED;
    }
    uint32_t available = core1_smaller(step, dsd->byte_count - dsd->byte_position);
    if (dsd->packet_cursor == dsd->packet_frames) {
        Core1DecodeResult result = packet(dsd);
        if (result != CORE1_DECODED) {
            return result;
        }
    }
    if (available > dsd->packet_frames - dsd->packet_cursor) {
        return CORE1_FAILED;
    }
    dsd->packet_cursor += available;
    return CORE1_DECODED;
}

static int32_t scaled(const Core1Dsd *dsd, int32_t value)
{
    return (int32_t)((uint32_t)value << dsd->raise) >> dsd->lower;
}

static void remember(Core1Dsd *dsd, const int32_t *values)
{
    int32_t *recent = dsd->work->history[dsd->stage_position];
    int32_t *older = dsd->work->history[dsd->stage_position + DSD_SECOND_TAPS];
    int32_t left = scaled(dsd, values[0]);
    int32_t right = scaled(dsd, values[1]);
    recent[0] = left;
    recent[1] = right;
    older[0] = left;
    older[1] = right;
}

static Core1DecodeResult advance(Core1Dsd *dsd)
{
    if (dsd->byte_position < dsd->byte_count) {
        Core1DecodeResult result = take(dsd);
        if (result != CORE1_DECODED) {
            return result;
        }
    }
    dsd->byte_position += 2 * dsd->ratio;
    int32_t values[2];
    first_stages(dsd, values);
    remember(dsd, values);
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
    second_stage(dsd);
    dsd->remaining--;
    dsd->staged++;
    return CORE1_DECODED;
}

static uint8_t dop_byte(const Core1Dsd *dsd, const uint8_t *data, uint32_t position)
{
    if (position >= dsd->byte_count) {
        return 0x69;
    }
    uint8_t value = reverse(ordered(dsd, data[position - dsd->lane_start]));
    if (position == dsd->steady_end && dsd->partial_bits != 0) {
        unsigned mask = 0xffu >> dsd->partial_bits;
        value = (uint8_t)((value & ~mask) | (0x69u & mask));
    }
    return value;
}

static uint32_t dop_pair(const uint8_t *bytes, bool lsb_first)
{
    if (!lsb_first) {
        return bytes_be16(bytes);
    }
#if defined(__arm__)
    uint32_t value = bytes_le16(bytes);
    __asm__("rbit %0, %1" : "=r"(value) : "r"(value));
    return value >> 16;
#else
    return (uint32_t)reverse(bytes[0]) << 8 | reverse(bytes[1]);
#endif
}

static int32_t dop_marker(const Core1Dsd *dsd)
{
    return ((dsd->first + dsd->staged) & 1u) == 0 ? 0x050000 : -0x060000;
}

static void dop_frames(const Core1Dsd *dsd, const uint8_t *bytes, int32_t *samples, uint32_t count)
{
    bool lsb_first = dsd->layout == 1;
    int32_t marker = dop_marker(dsd);
    for (uint32_t frame = 0; frame < count; frame++, bytes += 2) {
        samples[frame] = marker | (int32_t)dop_pair(bytes, lsb_first);
        marker ^= -0x10000;
    }
}

static __attribute__((noinline)) Core1DecodeResult pack_dop(Core1Dsd *dsd)
{
    int32_t (*output)[FLAC_MAX_BLOCK] = dsd->region.samples[dsd->slot];
    while (dsd->remaining != 0 && dsd->staged < DSD_OUTPUT_FRAMES) {
        if (dsd->packet_cursor == dsd->packet_frames) {
            Core1DecodeResult result = packet(dsd);
            if (result != CORE1_DECODED) {
                return result;
            }
        }
        uint32_t available = dsd->packet_frames - dsd->packet_cursor;
        uint32_t position = dsd->byte_position;
        uint32_t complete = core1_smaller(available, dsd->steady_end - position) / 2;
        uint32_t count = core1_smaller(complete, DSD_OUTPUT_FRAMES - dsd->staged);
        bool tail = count == 0;
        if (tail) {
            count = 1;
        }
        for (uint32_t channel = 0; channel < dsd->channels; channel++) {
            const uint8_t *data = lane(dsd, channel);
            int32_t *samples = &output[channel][dsd->staged];
            if (tail) {
                *samples = dop_marker(dsd) | (int32_t)dop_byte(dsd, data, position) << 8 |
                           dop_byte(dsd, data, position + 1);
            } else {
                dop_frames(dsd, data + (position - dsd->lane_start), samples, count);
            }
        }
        dsd->packet_cursor += core1_smaller(available, count * 2);
        dsd->byte_position += count * 2;
        dsd->remaining -= count;
        dsd->staged += count;
    }
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
    if (dsd->dop) {
        Core1DecodeResult result = pack_dop(dsd);
        if (result != CORE1_DECODED) {
            return result;
        }
    } else {
        while (dsd->remaining != 0 && dsd->staged < DSD_OUTPUT_FRAMES) {
            Core1DecodeResult result = advance(dsd);
            if (result != CORE1_DECODED) {
                return result;
            }
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
    return (Core1Decoder){start, decode, dsd, false};
}
