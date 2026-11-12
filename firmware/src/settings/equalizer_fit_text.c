#include "settings/equalizer_fit.h"

#include "equalizer_fit_internal.h"

enum {
    MAX_WHOLE = 99999
};

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
    equalizer_fit_end_number(fit);
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
    if (fit->line_start && (character == 'F' || character == 'f')) {
        fit->comment = true;
    }
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
