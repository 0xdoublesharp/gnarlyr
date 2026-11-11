#include "library/replay_gain.h"

#include <stddef.h>

#include "replay_gain_table.h"

enum {
    TEXT_LIMIT = REPLAY_GAIN_TEXT_CAPACITY - 1,
    KEPT_DIGITS = 12,
    GAIN_LIMIT = 9600,
    R128_TEXT_LIMIT = 6,
    R128_MAX = 32767,
    R128_SCALE = 256,
    R128_REFERENCE_OFFSET = 5 * R128_SCALE,
    CENTI_DB = 100,
    PEAK_SCALE = 16777216
};

typedef struct Number {
    uint64_t mantissa;
    int32_t exponent;
    uint32_t at;
    uint32_t start;
    uint32_t end;
    int32_t point;
    uint32_t digits;
    uint8_t kept;
    bool negative;
} Number;

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
            number->point += fraction ? 0 : 1;
            number->digits++;
            digits = true;
        } else if (value == '.' && !fraction) {
            fraction = true;
        } else {
            break;
        }
        number->at++;
    }
    number->end = number->at;
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
    number->point += negative ? -value : value;
    return number->at > start;
}

static bool number_of(const char *text, uint32_t length, bool units, Number *number)
{
    *number = (Number){.at = skip_space(text, length, 0)};
    number->negative = number->at < length && text[number->at] == '-';
    if (number->at < length && (number->negative || text[number->at] == '+')) {
        number->at++;
    }
    number->start = number->at;
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

static uint64_t fraction_value(const Number *number, uint64_t value)
{
    if (number->exponent < -19) {
        return 0;
    }
    uint64_t divisor = 1;
    for (int32_t step = 0; step > number->exponent; step--) {
        divisor *= 10;
    }
    uint64_t result = value / divisor;
    uint64_t remainder = value % divisor;
    bool increment = remainder >= divisor / 2;
    return result + (increment ? 1u : 0u);
}

static bool gain_value(const Number *number, uint32_t *out)
{
    uint64_t value = number->mantissa * 100;
    if (number->exponent < 0) {
        value = fraction_value(number, value);
    } else {
        for (int32_t step = 0; step < number->exponent; step++) {
            if (value > GAIN_LIMIT / 10) {
                return false;
            }
            value *= 10;
        }
    }
    if (value > GAIN_LIMIT) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static uint32_t peak_fraction(const char *text, const Number *number)
{
    uint32_t value = 0;
    int32_t position = (int32_t)number->digits;
    for (uint32_t at = number->end; at > number->start;) {
        char next = text[--at];
        if (next != '.' && --position >= number->point) {
            value = ((uint32_t)(next - '0') * PEAK_SCALE + value + 9) / 10;
        }
    }
    for (int32_t leading = number->point; leading < 0; leading++) {
        value = (value + 9) / 10;
    }
    return value;
}

static bool peak_integer(const char *text, const Number *number, uint32_t *out)
{
    uint32_t value = 0;
    int32_t position = 0;
    for (uint32_t at = number->start; at < number->end && position < number->point; at++) {
        if (text[at] != '.') {
            value = value * 10 + (uint32_t)(text[at] - '0');
            position++;
            if (value > UINT32_MAX / PEAK_SCALE) {
                return false;
            }
        }
    }
    for (; position < number->point; position++) {
        if (value > UINT32_MAX / PEAK_SCALE / 10) {
            return false;
        }
        value *= 10;
    }
    *out = value;
    return true;
}

static bool peak_value(const char *text, const Number *number, uint32_t *out)
{
    uint32_t whole;
    if (!peak_integer(text, number, &whole)) {
        return false;
    }
    uint64_t value = (uint64_t)whole * PEAK_SCALE + peak_fraction(text, number);
    if (value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool replay_gain_offer(ReplayGainTags *tags, ReplayGainField field, const char *text,
                       uint32_t length)
{
    if (tags == NULL || text == NULL || length == 0 || length > TEXT_LIMIT ||
        (uint32_t)field >= REPLAY_GAIN_NO_FIELD ||
        (tags->present & (uint8_t)~tags->r128 & (1u << field)) != 0) {
        return false;
    }
    bool gain = field <= REPLAY_GAIN_ALBUM_GAIN;
    Number number;
    uint32_t value;
    if (!number_of(text, length, gain, &number) ||
        !(gain ? gain_value(&number, &value) : peak_value(text, &number, &value)) ||
        (!gain && number.negative && value != 0)) {
        return false;
    }
    if (gain) {
        tags->gain[field] = (int16_t)(number.negative ? -(int32_t)value : (int32_t)value);
    } else {
        tags->peak[field - REPLAY_GAIN_TRACK_PEAK] = value;
    }
    tags->present |= (uint8_t)(1u << field);
    tags->r128 &= (uint8_t)~(1u << field);
    return true;
}

static bool r128_value(const char *text, uint32_t length, int32_t *value)
{
    if (length == 0 || length > R128_TEXT_LIMIT) {
        return false;
    }
    bool negative = text[0] == '-';
    uint32_t first = negative || text[0] == '+' ? 1u : 0u;
    uint32_t number = 0;
    for (uint32_t at = first; at < length; at++) {
        if (!digit(text[at])) {
            return false;
        }
        number = number * 10 + (uint32_t)(text[at] - '0');
    }
    if (first == length || number > R128_MAX + (negative ? 1u : 0u)) {
        return false;
    }
    *value = negative ? -(int32_t)number : (int32_t)number;
    return true;
}

bool replay_gain_offer_r128(ReplayGainTags *tags, ReplayGainField field, const char *text,
                            uint32_t length)
{
    int32_t value;
    if (tags == NULL || text == NULL || (uint32_t)field > REPLAY_GAIN_ALBUM_GAIN ||
        (tags->present & (1u << field)) != 0 || !r128_value(text, length, &value)) {
        return false;
    }
    int32_t scaled = (value + R128_REFERENCE_OFFSET) * CENTI_DB;
    int32_t gain = (scaled + (scaled < 0 ? -R128_SCALE / 2 : R128_SCALE / 2)) / R128_SCALE;
    if (gain < -GAIN_LIMIT || gain > GAIN_LIMIT) {
        return false;
    }
    tags->gain[field] = (int16_t)gain;
    tags->present |= (uint8_t)(1u << field);
    tags->r128 |= (uint8_t)(1u << field);
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
