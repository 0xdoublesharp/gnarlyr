#include "device/dsd_stream.h"

static uint64_t little(const uint8_t *bytes, uint32_t count)
{
    uint64_t value = 0;
    for (uint32_t index = count; index > 0; index--) {
        value = value << 8 | bytes[index - 1];
    }
    return value;
}

static uint64_t big(const uint8_t *bytes, uint32_t count)
{
    uint64_t value = 0;
    for (uint32_t index = 0; index < count; index++) {
        value = value << 8 | bytes[index];
    }
    return value;
}

static bool tag(const uint8_t *bytes, const char *text)
{
    return big(bytes, 4) == big((const uint8_t *)text, 4);
}

static bool dsf(FlacInput input, DsdTrack *track)
{
    uint8_t bytes[80];
    if (input.size < 92 || !input.read(input.context, 0, bytes, sizeof bytes) ||
        !tag(bytes, "DSD ") || little(bytes + 4, 8) != 28 || little(bytes + 12, 8) != input.size ||
        !tag(bytes + 28, "fmt ") || little(bytes + 40, 4) != 1 || little(bytes + 44, 4) != 0 ||
        little(bytes + 72, 4) != DSD_BLOCK_BYTES) {
        return false;
    }
    uint64_t format_bytes = little(bytes + 32, 8);
    uint64_t metadata = little(bytes + 20, 8);
    uint32_t order = (uint32_t)little(bytes + 60, 4);
    uint32_t channels = (uint32_t)little(bytes + 52, 4);
    if (format_bytes < 52 || format_bytes > input.size - 40 || (order != 1 && order != 8) ||
        channels < 1 || channels > 2 || little(bytes + 48, 4) != channels) {
        return false;
    }
    *track = (DsdTrack){.samples = little(bytes + 64, 8),
                        .rate = (uint32_t)little(bytes + 56, 4),
                        .channels = channels,
                        .layout = order == 1 ? 1u : 2u,
                        .data_offset = 40 + (uint32_t)format_bytes};
    uint32_t chunk = 28 + (uint32_t)format_bytes;
    if (!input.read(input.context, chunk, bytes, 12) || !tag(bytes, "data")) {
        return false;
    }
    uint64_t size = little(bytes + 4, 8);
    if (size < 12 || size > input.size - chunk ||
        (metadata != 0 && (metadata < chunk + size || metadata >= input.size))) {
        return false;
    }
    track->data_bytes = (uint32_t)size - 12;
    uint64_t blocks = track->samples / 32768 + (track->samples % 32768 != 0);
    return blocks <= track->data_bytes / (DSD_BLOCK_BYTES * channels);
}

static bool properties(FlacInput input, uint32_t body, uint32_t end, DsdTrack *track)
{
    uint8_t bytes[18];
    if (end - body < 4 || !input.read(input.context, body, bytes, 4) || !tag(bytes, "SND ")) {
        return false;
    }
    bool compressed = true;
    for (uint32_t cursor = body + 4; end - cursor >= 12;) {
        if (!input.read(input.context, cursor, bytes, 12)) {
            return false;
        }
        uint64_t size = big(bytes + 4, 8);
        uint32_t start = cursor + 12;
        if (size > end - start || (size & 1u) > end - start - size) {
            return false;
        }
        uint32_t length = (uint32_t)size;
        uint32_t count = length < 6 ? length : 6;
        if (!input.read(input.context, start, bytes + 12, count)) {
            return false;
        }
        if (tag(bytes, "FS  ") && length == 4) {
            track->rate = (uint32_t)big(bytes + 12, 4);
        } else if (tag(bytes, "CHNL") && length >= 2) {
            track->channels = (uint32_t)big(bytes + 12, 2);
            if (track->channels > 2 || length < 2 + 4 * track->channels) {
                return false;
            }
        } else if (tag(bytes, "CMPR") && length >= 4) {
            compressed = !tag(bytes + 12, "DSD ");
        }
        cursor = start + length + (length & 1u);
    }
    return !compressed;
}

static bool dff(FlacInput input, DsdTrack *track)
{
    uint8_t bytes[16];
    if (input.size < 16 || !input.read(input.context, 0, bytes, 16) || !tag(bytes, "FRM8") ||
        !tag(bytes + 12, "DSD ")) {
        return false;
    }
    uint64_t size = big(bytes + 4, 8);
    if (size < 4 || size > input.size - 12) {
        return false;
    }
    uint32_t end = 12 + (uint32_t)size;
    bool have_properties = false;
    for (uint32_t cursor = 16; end - cursor >= 12;) {
        if (!input.read(input.context, cursor, bytes, 12)) {
            return false;
        }
        uint64_t chunk = big(bytes + 4, 8);
        uint32_t body = cursor + 12;
        if (chunk > end - body || (chunk & 1u) > end - body - chunk) {
            return false;
        }
        uint32_t length = (uint32_t)chunk;
        if (tag(bytes, "PROP")) {
            if (have_properties || !properties(input, body, body + length, track)) {
                return false;
            }
            have_properties = true;
        } else if (tag(bytes, "DSD ")) {
            if (track->data_bytes != 0) {
                return false;
            }
            track->data_offset = body;
            track->data_bytes = length;
        } else if (tag(bytes, "DST ")) {
            return false;
        }
        cursor = body + length + (length & 1u);
    }
    if (!have_properties || track->channels == 0 || track->data_bytes % track->channels != 0) {
        return false;
    }
    track->samples = (uint64_t)(track->data_bytes / track->channels) * 8;
    return true;
}

bool dsd_probe(FlacInput input, DsdTrack *track)
{
    DsdTrack candidate = {0};
    bool parsed = dsf(input, &candidate);
    if (!parsed) {
        candidate = (DsdTrack){0};
        parsed = dff(input, &candidate);
    }
    if (!parsed || candidate.samples == 0 ||
        (candidate.rate != 2822400 && candidate.rate != 5644800 && candidate.rate != 11289600)) {
        return false;
    }
    uint32_t decimation = candidate.rate / DSD_PCM_RATE;
    if ((candidate.samples - 1) / decimation >= UINT32_MAX) {
        return false;
    }
    *track = candidate;
    return true;
}
