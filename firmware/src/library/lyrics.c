#include "library/lyrics.h"

#include "core/utf8.h"

enum {
    READ_BYTES = 128,
    PREFIX_BYTES = 128,
    TAGS_PER_LINE = 8,
    OFFSET_LIMIT_MS = 3600000
};

typedef struct LyricsParse {
    LyricsIndex *index;
    char prefix[PREFIX_BYTES];
    uint32_t start;
    uint32_t bytes;
    uint16_t kept;
    int32_t offset_ms;
} LyricsParse;

static bool digit(char value)
{
    return value >= '0' && value <= '9';
}

static bool number(const char *text, uint16_t length, uint16_t *at, uint32_t *value)
{
    uint16_t start = *at;
    *value = 0;
    while (*at < length && digit(text[*at])) {
        if (*value > (UINT32_MAX - (uint32_t)(text[*at] - '0')) / 10) {
            return false;
        }
        *value = *value * 10 + (uint32_t)(text[(*at)++] - '0');
    }
    return *at > start;
}

static bool fraction(const char *text, uint16_t length, uint16_t *at, uint32_t *value)
{
    *value = 0;
    if (*at >= length || text[*at] != '.') {
        return true;
    }
    uint16_t start = ++*at;
    uint32_t scale = 100;
    while (*at < length && digit(text[*at]) && *at - start < 3) {
        *value += (uint32_t)(text[(*at)++] - '0') * scale;
        scale /= 10;
    }
    return *at > start && (*at >= length || !digit(text[*at]));
}

static bool timestamp(const char *text, uint16_t length, uint16_t *at, uint32_t *value)
{
    uint16_t cursor = *at;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t millis;
    if (cursor >= length || text[cursor++] != '[' || !number(text, length, &cursor, &minutes) ||
        minutes > 9999 || cursor >= length || text[cursor++] != ':') {
        return false;
    }
    uint16_t start = cursor;
    if (!number(text, length, &cursor, &seconds) || cursor - start != 2 || seconds >= 60 ||
        !fraction(text, length, &cursor, &millis) || cursor >= length || text[cursor++] != ']') {
        return false;
    }
    *at = cursor;
    *value = minutes * 60000 + seconds * 1000 + millis;
    return true;
}

static bool offset_tag(LyricsParse *parse)
{
    static const char PREFIX[] = "[offset:";
    if (parse->kept < sizeof PREFIX + 1) {
        return false;
    }
    for (uint32_t index = 0; index + 1u < sizeof PREFIX; index++) {
        if (parse->prefix[index] != PREFIX[index]) {
            return false;
        }
    }
    uint16_t at = sizeof PREFIX - 1;
    bool negative = parse->prefix[at] == '-';
    if (negative || parse->prefix[at] == '+') {
        at++;
    }
    uint32_t value;
    if (number(parse->prefix, parse->kept, &at, &value) && value <= OFFSET_LIMIT_MS &&
        at < parse->kept && parse->prefix[at] == ']') {
        parse->offset_ms = negative ? -(int32_t)value : (int32_t)value;
    }
    return true;
}

static void add_line(LyricsIndex *index, LyricsLine line)
{
    if (index->count == LYRICS_LINE_CAPACITY) {
        index->truncated = true;
        return;
    }
    uint16_t at = index->count++;
    while (at > 0 && index->lines[at - 1].time_ms > line.time_ms) {
        index->lines[at] = index->lines[at - 1];
        at--;
    }
    index->lines[at] = line;
}

static void parse_line(LyricsParse *parse)
{
    bool bom = parse->start == 0 && parse->kept >= 3 && (uint8_t)parse->prefix[0] == 0xef &&
               (uint8_t)parse->prefix[1] == 0xbb && (uint8_t)parse->prefix[2] == 0xbf;
    if (bom) {
        for (uint16_t at = 0; at + 3 < parse->kept; at++) {
            parse->prefix[at] = parse->prefix[at + 3];
        }
        parse->kept -= 3;
        parse->bytes -= 3;
        parse->start += 3;
    }
    if (offset_tag(parse)) {
        return;
    }
    uint16_t at = 0;
    uint32_t times[TAGS_PER_LINE];
    uint16_t count = 0;
    while (count < TAGS_PER_LINE && timestamp(parse->prefix, parse->kept, &at, &times[count])) {
        count++;
    }
    uint32_t bytes = parse->bytes - at;
    for (uint16_t index = 0; index < count; index++) {
        add_line(parse->index, (LyricsLine){times[index], parse->start + at,
                                            (uint16_t)(bytes > UINT16_MAX ? UINT16_MAX : bytes)});
    }
}

static void feed(LyricsParse *parse, char byte, uint32_t position)
{
    if (byte == '\n' || byte == '\r') {
        parse_line(parse);
        parse->start = position + 1;
        parse->bytes = 0;
        parse->kept = 0;
        return;
    }
    if (parse->kept < PREFIX_BYTES) {
        parse->prefix[parse->kept++] = byte;
    }
    parse->bytes++;
}

static void apply_offset(LyricsParse *parse)
{
    for (uint16_t index = 0; index < parse->index->count; index++) {
        uint32_t *time = &parse->index->lines[index].time_ms;
        if (parse->offset_ms < 0) {
            uint32_t offset = (uint32_t)-parse->offset_ms;
            *time = *time > offset ? *time - offset : 0;
        } else {
            uint32_t offset = (uint32_t)parse->offset_ms;
            *time = UINT32_MAX - *time < offset ? UINT32_MAX : *time + offset;
        }
    }
}

bool lyrics_parse(LyricsIndex *index, ByteSource source, uint32_t bytes)
{
    *index = (LyricsIndex){0};
    if (source.read == NULL || bytes > LYRICS_FILE_BYTES) {
        return false;
    }
    LyricsParse parse = {.index = index};
    char chunk[READ_BYTES];
    for (uint32_t offset = 0; offset < bytes; offset += READ_BYTES) {
        uint32_t count = bytes - offset < READ_BYTES ? bytes - offset : READ_BYTES;
        if (!source.read(source.context, offset, chunk, count)) {
            *index = (LyricsIndex){0};
            return false;
        }
        for (uint32_t byte = 0; byte < count; byte++) {
            feed(&parse, chunk[byte], offset + byte);
        }
    }
    parse_line(&parse);
    apply_offset(&parse);
    return true;
}

int lyrics_current(const LyricsIndex *index, uint32_t position_ms)
{
    int low = 0;
    int high = index->count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (index->lines[middle].time_ms <= position_ms) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low - 1;
}

bool lyrics_text(ByteSource source, const LyricsLine *line, char *text, size_t capacity)
{
    if (capacity == 0) {
        return false;
    }
    text[0] = '\0';
    uint32_t count = line->bytes < capacity - 1 ? line->bytes : (uint32_t)(capacity - 1);
    if (source.read == NULL || !source.read(source.context, line->offset, text, count)) {
        text[0] = '\0';
        return false;
    }
    text[utf8_complete_length(text, count)] = '\0';
    return true;
}
