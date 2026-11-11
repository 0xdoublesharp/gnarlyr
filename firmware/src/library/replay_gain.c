#include "library/replay_gain.h"

#include <stddef.h>

#include "replay_gain_table.h"

enum {
    TEXT_LIMIT = REPLAY_GAIN_TEXT_CAPACITY - 1,
    KEPT_DIGITS = 12,
    GAIN_LIMIT = 9600,
    PEAK_SCALE = 16777216
};

typedef struct Number {
    uint64_t mantissa;
    int32_t exponent;
    uint32_t at;
    uint8_t kept;
    bool negative;
    bool extra;
} Number;

typedef struct FixedScale {
    uint32_t scale;
    uint32_t maximum;
    bool ceiling;
} FixedScale;

static const char *const KEYS[] = {"replaygain_track_gain", "replaygain_album_gain",
                                   "replaygain_track_peak", "replaygain_album_peak"};

static char lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value - 'A' + 'a') : value;
}

ReplayGainField replay_gain_field(const char *key, uint32_t length)
{
    if (key == NULL) {
        return REPLAY_GAIN_NO_FIELD;
    }
    for (uint32_t field = 0; field < REPLAY_GAIN_NO_FIELD; field++) {
        uint32_t at = 0;
        while (at < length && KEYS[field][at] != '\0' && lower(key[at]) == KEYS[field][at]) {
            at++;
        }
        if (at == length && KEYS[field][at] == '\0') {
            return (ReplayGainField)field;
        }
    }
    return REPLAY_GAIN_NO_FIELD;
}

static bool digit(char value)
{
    return value >= '0' && value <= '9';
}

static uint32_t skip_space(const char *text, uint32_t length, uint32_t at)
{
    while (at < length && (text[at] == ' ' || (text[at] >= 9 && text[at] <= 13))) {
        at++;
    }
    return at;
}

static void accumulate(Number *number, uint32_t value, bool fraction)
{
    number->exponent -= fraction ? 1 : 0;
    if (number->kept < KEPT_DIGITS) {
        if (number->mantissa != 0 || value != 0) {
            number->mantissa = number->mantissa * 10 + value;
            number->kept++;
        }
    } else {
        number->exponent++;
        number->extra = number->extra || value != 0;
    }
}

static bool decimal_part(const char *text, uint32_t length, Number *number)
{
    bool fraction = false;
    bool digits = false;
    while (number->at < length) {
        char value = text[number->at];
        if (digit(value)) {
            accumulate(number, (uint32_t)(value - '0'), fraction);
            digits = true;
        } else if (value == '.' && !fraction) {
            fraction = true;
        } else {
            break;
        }
        number->at++;
    }
    return digits;
}

static bool exponent_part(const char *text, uint32_t length, Number *number)
{
    if (number->at == length || lower(text[number->at]) != 'e') {
        return true;
    }
    number->at++;
    bool negative = number->at < length && text[number->at] == '-';
    if (number->at < length && (negative || text[number->at] == '+')) {
        number->at++;
    }
    uint32_t start = number->at;
    int32_t value = 0;
    while (number->at < length && digit(text[number->at])) {
        value = value * 10 + text[number->at++] - '0';
        if (value > 99) {
            return false;
        }
    }
    number->exponent += negative ? -value : value;
    return number->at > start;
}

static bool number_of(const char *text, uint32_t length, bool units, Number *number)
{
    *number = (Number){.at = skip_space(text, length, 0)};
    number->negative = number->at < length && text[number->at] == '-';
    if (number->at < length && (number->negative || text[number->at] == '+')) {
        number->at++;
    }
    if (!decimal_part(text, length, number) || !exponent_part(text, length, number)) {
        return false;
    }
    number->at = skip_space(text, length, number->at);
    if (units && length - number->at >= 2 && lower(text[number->at]) == 'd' &&
        lower(text[number->at + 1]) == 'b') {
        number->at = skip_space(text, length, number->at + 2);
    }
    return number->at == length;
}

static uint64_t fraction_value(const Number *number, uint64_t value, bool ceiling)
{
    if (number->exponent < -19) {
        return ceiling && (value != 0 || number->extra) ? 1 : 0;
    }
    uint64_t divisor = 1;
    for (int32_t step = 0; step > number->exponent; step--) {
        divisor *= 10;
    }
    uint64_t result = value / divisor;
    uint64_t remainder = value % divisor;
    bool increment = ceiling ? remainder != 0 || number->extra : remainder >= divisor / 2;
    return result + (increment ? 1u : 0u);
}

static bool fixed_value(const Number *number, FixedScale scale, uint32_t *out)
{
    uint64_t value = number->mantissa * scale.scale;
    if (number->exponent < 0) {
        value = fraction_value(number, value, scale.ceiling);
    } else {
        for (int32_t step = 0; step < number->exponent; step++) {
            if (value > scale.maximum / 10) {
                return false;
            }
            value *= 10;
        }
    }
    if (value > scale.maximum) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool replay_gain_offer(ReplayGainTags *tags, ReplayGainField field, const char *text,
                       uint32_t length)
{
    if (tags == NULL || text == NULL || length == 0 || length > TEXT_LIMIT ||
        (uint32_t)field >= REPLAY_GAIN_NO_FIELD || (tags->present & (1u << field)) != 0) {
        return false;
    }
    bool gain = field <= REPLAY_GAIN_ALBUM_GAIN;
    FixedScale scale =
        gain ? (FixedScale){100, GAIN_LIMIT, false} : (FixedScale){PEAK_SCALE, UINT32_MAX, true};
    Number number;
    uint32_t value;
    if (!number_of(text, length, gain, &number) || !fixed_value(&number, scale, &value) ||
        (!gain && number.negative && value != 0)) {
        return false;
    }
    if (gain) {
        tags->gain[field] = number.negative ? -(int16_t)value : (int16_t)value;
    } else {
        tags->peak[field - REPLAY_GAIN_TRACK_PEAK] = value;
    }
    tags->present |= (uint8_t)(1u << field);
    return true;
}

uint32_t replay_gain_amplitude(int16_t centi_db)
{
    if (centi_db < -GAIN_LIMIT || centi_db > GAIN_LIMIT) {
        return 0;
    }
    bool negative = centi_db < 0;
    uint32_t amount = (uint32_t)(negative ? -centi_db : centi_db);
    uint64_t gain = REPLAY_GAIN_UNITY;
    for (uint32_t bit = 0; amount != 0; bit++, amount >>= 1) {
        if ((amount & 1u) != 0) {
            gain = (gain * REPLAY_GAIN_POWERS[negative ? 1 : 0][bit] + (UINT64_C(1) << 31)) >> 32;
        }
    }
    return gain == 0 ? 1 : (uint32_t)gain;
}

static uint32_t safe_gain(const ReplayGainTags *tags, uint32_t index, uint32_t gain)
{
    uint32_t peak_index = index;
    if ((tags->present & (1u << (peak_index + REPLAY_GAIN_TRACK_PEAK))) == 0) {
        peak_index = 1 - index;
    }
    if ((tags->present & (1u << (peak_index + REPLAY_GAIN_TRACK_PEAK))) == 0) {
        return gain < REPLAY_GAIN_UNITY ? gain : REPLAY_GAIN_UNITY;
    }
    uint32_t peak = tags->peak[peak_index];
    uint64_t limit = peak == 0 ? UINT32_MAX : (UINT64_C(1) << 40) / peak;
    return gain < limit ? gain : (uint32_t)limit;
}

ReplayGain replay_gain_finish(const ReplayGainTags *tags)
{
    ReplayGain gain = {
        .track = (tags->present & (1u << REPLAY_GAIN_TRACK_GAIN)) != 0
                     ? replay_gain_amplitude(tags->gain[0])
                     : 0,
        .album = (tags->present & (1u << REPLAY_GAIN_ALBUM_GAIN)) != 0
                     ? replay_gain_amplitude(tags->gain[1])
                     : 0,
    };
    gain.track_safe = safe_gain(tags, 0, gain.track);
    gain.album_safe = safe_gain(tags, 1, gain.album);
    return gain;
}
