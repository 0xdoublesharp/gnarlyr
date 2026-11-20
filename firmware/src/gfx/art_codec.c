#include "gfx/art_codec.h"

typedef struct ArtCursor {
    Color *out;
    int x;
    int first;
    int last;
} ArtCursor;

static ArtCursor cursor_for(const ArtSpan *span, Color *out, int x, int stop)
{
    int first = x > span->from ? x : span->from;
    int last = stop < span->to ? stop : span->to;
    return (ArtCursor){out, x, first, last > first ? last : first};
}

static Color *put_run(Color *out, Color color, uint32_t alpha, int count)
{
    Color *end = out + count;
    if (alpha >= COLOR_ALPHA_OPAQUE) {
        while (out < end) {
            *out++ = color;
        }
        return end;
    }
    for (; out < end; out++) {
        *out = color_blend(*out, color, (uint8_t)alpha);
    }
    return end;
}

static uint32_t entry_alpha(const ArtPalette *palette, uint32_t index)
{
    return index < palette->alpha_entries ? palette->alpha[index >> 1] >> ((index & 1) * 4) & 15u
                                          : COLOR_ALPHA_OPAQUE;
}

static uint32_t put_indices(Color *out, const ArtPalette *palette, const uint8_t *source, int count)
{
    uint32_t seen = 0;
    const Color *colors = palette->colors;
    if (palette->alpha_entries == 0 && palette->entries >= ART_PALETTE_MAX) {
        for (const uint8_t *end = source + count; source < end;) {
            *out++ = colors[*source++];
        }
        return 0;
    }
    if (palette->alpha_entries == 0) {
        for (const uint8_t *end = source + count; source < end;) {
            uint32_t index = *source++;
            seen |= index;
            *out++ = colors[index];
        }
        return seen;
    }
    for (int at = 0; at < count; at++) {
        seen |= source[at];
        uint32_t alpha = entry_alpha(palette, source[at]);
        if (alpha != 0) {
            put_run(out + at, palette->colors[source[at]], alpha, 1);
        }
    }
    return seen;
}

static bool indices_known(const uint8_t *source, int count, uint32_t entries)
{
    for (int at = 0; at < count; at++) {
        if (source[at] >= entries) {
            return false;
        }
    }
    return true;
}

bool art_row_indexed(ArtRow *row, const ArtPalette *palette, ArtSpan span)
{
    const uint8_t *at = row->at;
    Color *out = span.out;
    for (int x = 0; x < row->width;) {
        if (at >= row->end) {
            return false;
        }
        uint32_t packet = *at++;
        int count = (int)(packet & 0x7Fu) + 1;
        int bytes = packet & 0x80u ? 1 : count;
        if (count > row->width - x || row->end - at < bytes) {
            return false;
        }
        ArtCursor cursor = cursor_for(&span, out, x, x + count);
        int shown = cursor.last - cursor.first;
        if (packet & 0x80u) {
            uint32_t alpha = entry_alpha(palette, *at);
            if (*at >= palette->entries) {
                return false;
            }
            out = alpha != 0 ? put_run(out, palette->colors[*at], alpha, shown) : out + shown;
        } else {
            const uint8_t *source = at + (cursor.first - x);
            if (put_indices(out, palette, source, shown) >= palette->entries &&
                !indices_known(source, shown, palette->entries)) {
                return false;
            }
            out += shown;
        }
        at += bytes;
        x += count;
    }
    row->at = at;
    return at == row->end;
}

static Color add_delta(Color previous, uint32_t delta)
{
    uint32_t half = delta >> 5;
    uint32_t red = ((uint32_t)(previous >> 11) + half + ((delta >> 2) & 3u) - 6u) & 31u;
    uint32_t green = ((uint32_t)(previous >> 5) + (delta >> 4) - 8u) & 63u;
    uint32_t blue = ((uint32_t)previous + half + (delta & 3u) - 6u) & 31u;
    return (Color)(red << 11 | green << 5 | blue);
}

typedef struct ArtLossless {
    const uint8_t *at;
    Color *out;
    Color previous;
    uint32_t alpha;
} ArtLossless;

static void lossless_put(ArtLossless *state, Color color)
{
    *state->out = state->alpha >= COLOR_ALPHA_OPAQUE
                      ? color
                      : color_blend(*state->out, color, (uint8_t)state->alpha);
    state->out++;
}

static void lossless_deltas(ArtLossless *state, const ArtCursor *cursor, int count)
{
    for (int at = 0; at < count; at++) {
        state->previous = add_delta(state->previous, state->at[at]);
        if (cursor->x + at >= cursor->first && cursor->x + at < cursor->last) {
            lossless_put(state, state->previous);
        }
    }
}

static void lossless_literals(ArtLossless *state, const ArtCursor *cursor, int count)
{
    const uint8_t *source = state->at + (cursor->first - cursor->x) * 2;
    const uint8_t *end = source + (cursor->last - cursor->first) * 2;
    if (state->alpha >= COLOR_ALPHA_OPAQUE) {
        for (Color *out = state->out; source < end; source += 2) {
            *out++ = (Color)(source[0] | source[1] << 8);
        }
        state->out += cursor->last - cursor->first;
    }
    for (; source < end; source += 2) {
        lossless_put(state, (Color)(source[0] | source[1] << 8));
    }
    state->previous = (Color)(state->at[count * 2 - 2] | state->at[count * 2 - 1] << 8);
}

static void lossless_packet(ArtLossless *state, uint32_t packet, const ArtCursor *cursor, int count)
{
    int shown = cursor->last - cursor->first;
    if (packet < 0x40u) {
        state->out = put_run(state->out, state->previous, state->alpha, shown);
    } else if (packet < 0x80u) {
        lossless_deltas(state, cursor, count);
    } else if (packet < 0xC0u) {
        lossless_literals(state, cursor, count);
    } else {
        state->out += shown;
    }
}

static bool lossless_prefix(ArtLossless *state, uint32_t packet, bool alpha)
{
    if (!alpha || packet == 0xE0u || packet > 0xEEu || state->alpha != COLOR_ALPHA_OPAQUE) {
        return false;
    }
    state->alpha = packet - 0xE0u;
    return true;
}

bool art_row_lossless(ArtRow *row, bool alpha, ArtSpan span)
{
    ArtLossless state = {row->at, span.out, 0, COLOR_ALPHA_OPAQUE};
    for (int x = 0; x < row->width;) {
        if (state.at >= row->end) {
            return false;
        }
        uint32_t packet = *state.at++;
        if (packet >= 0xE0u) {
            if (!lossless_prefix(&state, packet, alpha)) {
                return false;
            }
            continue;
        }
        static const uint8_t PER_PIXEL[4] = {0, 1, 2, 0};
        int count = packet < 0xC0u ? (int)(packet & 0x3Fu) + 1 : (int)(packet & 0x1Fu) + 1;
        int bytes = count * PER_PIXEL[packet >> 6];
        if ((packet >= 0xC0u && (!alpha || state.alpha != COLOR_ALPHA_OPAQUE)) ||
            count > row->width - x || row->end - state.at < bytes) {
            return false;
        }
        ArtCursor cursor = cursor_for(&span, state.out, x, x + count);
        lossless_packet(&state, packet, &cursor, count);
        state.alpha = COLOR_ALPHA_OPAQUE;
        state.at += bytes;
        x += count;
    }
    row->at = state.at;
    return state.at == row->end;
}

static int check_indexed(const uint8_t **at, const uint8_t *end, uint32_t entries)
{
    uint32_t packet = *(*at)++;
    int count = (int)(packet & 0x7Fu) + 1;
    int bytes = packet & 0x80u ? 1 : count;
    if (end - *at < bytes) {
        return 0;
    }
    for (int index = 0; index < bytes; index++) {
        if ((*at)[index] >= entries) {
            return 0;
        }
    }
    *at += bytes;
    return count;
}

static int check_lossless(const uint8_t **at, const uint8_t *end, bool alpha)
{
    uint32_t packet = *(*at)++;
    bool prefixed = packet >= 0xE1u && packet <= 0xEEu;
    if (prefixed && alpha && *at < end) {
        packet = *(*at)++;
        if (packet >= 0xC0u) {
            return 0;
        }
    }
    if ((prefixed && !alpha) || packet >= 0xE0u || (packet >= 0xC0u && !alpha)) {
        return 0;
    }
    static const uint8_t PER_PIXEL[4] = {0, 1, 2, 0};
    int count = (int)(packet & (packet < 0xC0u ? 0x3Fu : 0x1Fu)) + 1;
    int bytes = count * PER_PIXEL[packet >> 6];
    if (end - *at < bytes) {
        return 0;
    }
    *at += bytes;
    return count;
}

bool art_row_check(ArtRow row, uint8_t codec, uint32_t entries, bool alpha)
{
    if (codec != ART_CODEC_INDEXED && codec != ART_CODEC_LOSSLESS) {
        return false;
    }
    int x = 0;
    while (x < row.width && row.at < row.end) {
        int count = codec == ART_CODEC_INDEXED ? check_indexed(&row.at, row.end, entries)
                                               : check_lossless(&row.at, row.end, alpha);
        if (count == 0 || count > row.width - x) {
            return false;
        }
        x += count;
    }
    return x == row.width && row.at == row.end;
}
