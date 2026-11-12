#include "device/wav_source.h"

static uint32_t big(const uint8_t *bytes, uint32_t count)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < count; index++) {
        value = value << 8 | bytes[index];
    }
    return value;
}

static bool tag(const uint8_t *bytes, const char *text)
{
    return big(bytes, 4) == big((const uint8_t *)text, 4);
}

static bool common(FlacInput input, uint32_t body, uint32_t size, WavFormat *format)
{
    uint8_t bytes[22];
    uint32_t count = format->aifc ? 22u : 18u;
    if (size < count || !input.read(input.context, body, bytes, count)) {
        return false;
    }
    uint32_t exponent = big(bytes + 8, 2);
    uint32_t mantissa = big(bytes + 10, 4);
    if (exponent < 16395 || exponent > 16400 || (mantissa & 0x80000000u) == 0 ||
        big(bytes + 14, 4) != 0) {
        return false;
    }
    uint32_t shift = 16414 - exponent;
    uint32_t rate = mantissa >> shift;
    uint32_t channels = big(bytes, 2);
    uint32_t bits = big(bytes + 6, 2);
    if ((rate << shift) != mantissa || rate < 8000 || rate > 192000 || channels < 1 ||
        channels > 2 || (bits != 16 && bits != 24)) {
        return false;
    }
    format->big_endian = !format->aifc || tag(bytes + 18, "NONE");
    if (format->aifc && !format->big_endian && !tag(bytes + 18, "sowt")) {
        return false;
    }
    format->sample_rate = rate;
    format->channels = (uint8_t)channels;
    format->bits = (uint8_t)bits;
    format->frame_bytes = (uint16_t)(channels * bits / 8);
    format->frames = big(bytes + 2, 4);
    return true;
}

bool aiff_probe(FlacInput input, WavFormat *format)
{
    uint8_t bytes[12];
    if (input.size < 12 || !input.read(input.context, 0, bytes, 12) || !tag(bytes, "FORM")) {
        return false;
    }
    uint32_t size = big(bytes + 4, 4);
    if (size < 4 || size > input.size - 8 || (!tag(bytes + 8, "AIFF") && !tag(bytes + 8, "AIFC"))) {
        return false;
    }
    *format = (WavFormat){.aifc = tag(bytes + 8, "AIFC")};
    uint32_t end = size + 8;
    bool have_common = false;
    bool have_sound = false;
    uint32_t sound_bytes = 0;
    for (uint32_t cursor = 12, chunks = 0; chunks < 64 && end - cursor >= 8; chunks++) {
        if (!input.read(input.context, cursor, bytes, 8)) {
            return false;
        }
        uint32_t length = big(bytes + 4, 4);
        uint32_t body = cursor + 8;
        if (length > end - body) {
            return false;
        }
        if (tag(bytes, "COMM")) {
            if (have_common || !common(input, body, length, format)) {
                return false;
            }
            have_common = true;
        } else if (tag(bytes, "SSND")) {
            if (have_sound || length < 8 || !input.read(input.context, body, bytes, 8)) {
                return false;
            }
            uint32_t offset = big(bytes, 4);
            if (offset > length - 8) {
                return false;
            }
            format->data_offset = body + 8 + offset;
            sound_bytes = length - 8 - offset;
            have_sound = true;
        }
        uint32_t step = length + (length & 1u);
        if (step > end - body) {
            return false;
        }
        cursor = body + step;
    }
    return have_common && have_sound && format->frames <= sound_bytes / format->frame_bytes;
}
