#include "settings/parametric_import.h"

typedef struct Tokens {
    const char *at;
    const char *word;
    uint16_t length;
} Tokens;

static char upper(char value)
{
    return value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
}

static bool next(Tokens *tokens)
{
    while (*tokens->at == ' ' || *tokens->at == '\t' || *tokens->at == '\r' || *tokens->at == ':') {
        tokens->at++;
    }
    tokens->word = tokens->at;
    while (*tokens->at != '\0' && *tokens->at != ' ' && *tokens->at != '\t' &&
           *tokens->at != '\r' && *tokens->at != ':') {
        tokens->at++;
    }
    tokens->length = (uint16_t)(tokens->at - tokens->word);
    return tokens->length != 0;
}

static bool is(const Tokens *tokens, const char *word)
{
    uint16_t at = 0;
    while (at < tokens->length && word[at] != '\0' && upper(tokens->word[at]) == word[at]) {
        at++;
    }
    return at == tokens->length && word[at] == '\0';
}

static bool number(Tokens *tokens, int32_t *value)
{
    if (!next(tokens)) {
        return false;
    }
    const char *at = tokens->word;
    const char *end = at + tokens->length;
    bool negative = *at == '-';
    at += *at == '-' || *at == '+';
    int32_t whole = 0;
    int digits = 0;
    while (at < end && *at >= '0' && *at <= '9' && whole <= 100000) {
        whole = whole * 10 + *at++ - '0';
        digits++;
    }
    int32_t fraction = 0;
    int scale = 10;
    if (at < end && *at == '.') {
        at++;
        while (at < end && *at >= '0' && *at <= '9') {
            fraction += (*at++ - '0') * scale;
            scale /= 10;
            digits++;
        }
    }
    *value = (whole * 100 + fraction) * (negative ? -1 : 1);
    return at == end && digits > 0 && whole <= 100000;
}

static bool keyword(Tokens *tokens, const char *word)
{
    return next(tokens) && is(tokens, word);
}

static int magnitude(const ParametricFilter *filter)
{
    return filter->gain < 0 ? -filter->gain : filter->gain;
}

static void keep(ParametricImport *parser, ParametricFilter filter)
{
    parser->total++;
    int index = parser->eq.count;
    if (index == PARAMETRIC_FILTERS) {
        index = 0;
        for (int other = 1; other < PARAMETRIC_FILTERS; other++) {
            if (magnitude(&parser->eq.filters[other]) < magnitude(&parser->eq.filters[index])) {
                index = other;
            }
        }
        if (magnitude(&filter) <= magnitude(&parser->eq.filters[index])) {
            return;
        }
    } else {
        parser->eq.count++;
    }
    parser->eq.filters[index] = filter;
}

static bool filter_line(ParametricImport *parser, Tokens *tokens)
{
    int32_t value;
    if (!number(tokens, &value) || value < 100 || value % 100 != 0 || !next(tokens)) {
        return false;
    }
    if (is(tokens, "OFF")) {
        return true;
    }
    if (!is(tokens, "ON") || !next(tokens)) {
        return false;
    }
    ParametricFilter filter = {.enabled = true, .q = 71};
    if (is(tokens, "PK") || is(tokens, "PEQ")) {
        filter.type = PARAMETRIC_PEAK;
    } else if (is(tokens, "LSC") || is(tokens, "LS")) {
        filter.type = PARAMETRIC_LOW_SHELF;
    } else if (is(tokens, "HSC") || is(tokens, "HS")) {
        filter.type = PARAMETRIC_HIGH_SHELF;
    } else {
        return false;
    }
    if (!keyword(tokens, "FC") || !number(tokens, &value) || value < 2000 || value > 2000000) {
        return false;
    }
    filter.hz = (uint16_t)((value + 50) / 100);
    if (!keyword(tokens, "HZ") || !keyword(tokens, "GAIN") || !number(tokens, &value) ||
        value < -1200 || value > 1200) {
        return false;
    }
    filter.gain = (int16_t)((value + (value < 0 ? -5 : 5)) / 10);
    if (!keyword(tokens, "DB")) {
        return false;
    }
    if (next(tokens)) {
        if (!is(tokens, "Q") || !number(tokens, &value) || value < 30 || value > 1000) {
            return false;
        }
        filter.q = (uint16_t)value;
        if (next(tokens) && tokens->word[0] != '#') {
            return false;
        }
    } else if (filter.type == PARAMETRIC_PEAK) {
        return false;
    }
    if (!parametric_filter_valid(&filter)) {
        return false;
    }
    keep(parser, filter);
    return true;
}

static void line(ParametricImport *parser)
{
    parser->line[parser->length] = '\0';
    Tokens tokens = {.at = parser->line};
    if (next(&tokens) && is(&tokens, "PREAMP")) {
        int32_t value;
        parser->recognized = true;
        bool valid =
            number(&tokens, &value) && value >= -6000 && value <= 1200 && keyword(&tokens, "DB");
        if (valid) {
            parser->eq.preamp = (int16_t)((value + (value < 0 ? -5 : 5)) / 10);
            valid = !next(&tokens) || tokens.word[0] == '#';
        }
        parser->invalid |= !valid;
    } else if (is(&tokens, "FILTER")) {
        parser->has_filters = true;
        parser->recognized = true;
        parser->invalid |= !filter_line(parser, &tokens);
    }
    parser->length = 0;
}

void parametric_import_start(ParametricImport *parser)
{
    *parser = (ParametricImport){0};
}

void parametric_import_text(ParametricImport *parser, const char *text, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (text[index] == '\n') {
            line(parser);
        } else if (parser->length + 1u < PARAMETRIC_LINE_BYTES) {
            parser->line[parser->length++] = text[index];
        } else {
            parser->invalid = true;
        }
    }
}

bool parametric_import_finish(ParametricImport *parser, ParametricEq *eq)
{
    line(parser);
    if (!parser->has_filters || parser->invalid || !parametric_valid(&parser->eq)) {
        return false;
    }
    *eq = parser->eq;
    return true;
}
