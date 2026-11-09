#include "settings/equalizer_fit.h"

enum {
    MIN_HZ = 10,
    MAX_HZ = 30000,
    MAX_WHOLE = 99999,
    GAIN_CAP_HUNDREDTHS = 30000,
    HUNDRED = 100,
    HALF_DB_HUNDREDTHS = 50,
    LOG_FRACTION_BITS = 8,
    LOG_UNIT_BITS = 15
};

const uint16_t SETTINGS_EQUALIZER_BAND_HZ[SETTINGS_EQUALIZER_BANDS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

void equalizer_fit_start(EqualizerFit *fit)
{
    *fit = (EqualizerFit){.line_start = true};
}

static EqualizerFitPoint point_of(uint32_t hz, int32_t hundredths)
{
    hundredths = hundredths > GAIN_CAP_HUNDREDTHS    ? GAIN_CAP_HUNDREDTHS
                 : hundredths < -GAIN_CAP_HUNDREDTHS ? -GAIN_CAP_HUNDREDTHS
                                                     : hundredths;
    return (EqualizerFitPoint){.hz = (uint16_t)hz, .hundredths = (int16_t)hundredths};
}

void equalizer_fit_point(EqualizerFit *fit, uint32_t hz, int32_t hundredths)
{
    if (hz < MIN_HZ || hz > MAX_HZ) {
        return;
    }
    EqualizerFitPoint point = point_of(hz, hundredths);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        uint32_t centre = SETTINGS_EQUALIZER_BAND_HZ[band];
        if (hz <= centre && hz > fit->below[band].hz) {
            fit->below[band] = point;
        }
        if (hz >= centre && (fit->above[band].hz == 0 || hz < fit->above[band].hz)) {
            fit->above[band] = point;
        }
    }
    fit->points++;
}

static void end_number(EqualizerFit *fit)
{
    if (!fit->in_number) {
        return;
    }
    int32_t value = (int32_t)(fit->whole * HUNDRED + fit->fraction);
    value = fit->negative ? -value : value;
    fit->in_number = false;
    if (fit->have_first) {
        equalizer_fit_point(fit, (uint32_t)(fit->first + HUNDRED / 2) / HUNDRED, value);
    }
    fit->first = value;
    fit->have_first = !fit->have_first;
}

static void begin_number(EqualizerFit *fit)
{
    fit->in_number = true;
    fit->in_fraction = false;
    fit->negative = fit->minus;
    fit->whole = 0;
    fit->fraction = 0;
    fit->fraction_digits = 0;
}

static void add_digit(EqualizerFit *fit, uint32_t digit)
{
    if (!fit->in_number) {
        begin_number(fit);
    }
    if (!fit->in_fraction) {
        fit->whole = fit->whole * 10 + digit;
        fit->whole = fit->whole > MAX_WHOLE ? MAX_WHOLE : fit->whole;
    } else if (fit->fraction_digits < 2) {
        fit->fraction += digit * (fit->fraction_digits == 0 ? 10 : 1);
        fit->fraction_digits++;
    }
}

static void other_character(EqualizerFit *fit, char character)
{
    end_number(fit);
    fit->minus = character == '-';
    if (character == '\n') {
        fit->have_first = false;
        fit->line_start = true;
        fit->comment = false;
    } else if (character == '#' && fit->line_start) {
        fit->comment = true;
    } else if (character != ' ' && character != '\t' && character != '\r') {
        fit->line_start = false;
    }
}

static void feed(EqualizerFit *fit, char character)
{
    if (fit->comment && character != '\n') {
        return;
    }
    if (character >= '0' && character <= '9') {
        add_digit(fit, (uint32_t)(character - '0'));
        fit->line_start = false;
        fit->minus = false;
    } else if (character == '.' && fit->in_number && !fit->in_fraction) {
        fit->in_fraction = true;
    } else {
        other_character(fit, character);
    }
}

void equalizer_fit_text(EqualizerFit *fit, const char *text, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        feed(fit, text[index]);
    }
}

static uint32_t log2_fixed(uint32_t hz)
{
    uint32_t whole = 0;
    while (hz >> (whole + 1) != 0) {
        whole++;
    }
    uint32_t mantissa = hz << (LOG_UNIT_BITS - whole);
    uint32_t log = whole;
    for (int bit = 0; bit < LOG_FRACTION_BITS; bit++) {
        mantissa = mantissa * mantissa >> LOG_UNIT_BITS;
        uint32_t carry = mantissa >> (LOG_UNIT_BITS + 1);
        mantissa >>= carry;
        log = log << 1 | carry;
    }
    return log;
}

static int32_t interpolated(EqualizerFitPoint below, EqualizerFitPoint above, uint32_t centre)
{
    if (below.hz == 0 || above.hz == 0 || below.hz == above.hz) {
        return below.hz == 0 ? above.hz == 0 ? 0 : above.hundredths : below.hundredths;
    }
    int32_t from = (int32_t)log2_fixed(below.hz);
    int32_t span = (int32_t)log2_fixed(above.hz) - from;
    int32_t at = (int32_t)log2_fixed(centre) - from;
    return below.hundredths + (above.hundredths - below.hundredths) * at / span;
}

static int8_t half_decibels(int32_t hundredths)
{
    int32_t half = hundredths + (hundredths < 0 ? -HALF_DB_HUNDREDTHS / 2 : HALF_DB_HUNDREDTHS / 2);
    half /= HALF_DB_HUNDREDTHS;
    half = half > SETTINGS_EQUALIZER_GAIN_MAX ? SETTINGS_EQUALIZER_GAIN_MAX : half;
    half = half < -SETTINGS_EQUALIZER_GAIN_MAX ? -SETTINGS_EQUALIZER_GAIN_MAX : half;
    return (int8_t)half;
}

bool equalizer_fit_finish(EqualizerFit *fit, int8_t gains[SETTINGS_EQUALIZER_BANDS])
{
    end_number(fit);
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        gains[band] = half_decibels(
            interpolated(fit->below[band], fit->above[band], SETTINGS_EQUALIZER_BAND_HZ[band]));
    }
    return fit->points > 0;
}
