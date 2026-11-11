#include "library/track_tags.h"

#include <stddef.h>

#include "tags_internal.h"

typedef struct FormatName {
    const char *extension;
    TagFormat format;
} FormatName;

static const FormatName FORMATS[] = {
    {"flac", TAG_FORMAT_FLAC}, {"ogg", TAG_FORMAT_OGG},   {"mp3", TAG_FORMAT_MP3},
    {"m4a", TAG_FORMAT_MP4},   {"m4b", TAG_FORMAT_MP4},   {"wav", TAG_FORMAT_WAV},
    {"wma", TAG_FORMAT_WMA},   {"ape", TAG_FORMAT_APE},   {"dsf", TAG_FORMAT_DSF},
    {"dff", TAG_FORMAT_DFF},   {"opus", TAG_FORMAT_OGG},  {"aiff", TAG_FORMAT_AIFF},
    {"aif", TAG_FORMAT_AIFF},  {"aifc", TAG_FORMAT_AIFF},
};

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static bool same_folded(const char *text, const char *wanted)
{
    for (; *wanted != '\0'; text++, wanted++) {
        if (lower(*text) != *wanted) {
            return false;
        }
    }
    return *text == '\0';
}

TagFormat track_tags_format_for_name(const char *name)
{
    const char *dot = NULL;
    for (const char *cursor = name; *cursor != '\0'; cursor++) {
        dot = *cursor == '.' ? cursor : dot;
    }
    if (dot == NULL || dot == name) {
        return TAG_FORMAT_UNKNOWN;
    }
    for (size_t index = 0; index < sizeof FORMATS / sizeof FORMATS[0]; index++) {
        if (same_folded(dot + 1, FORMATS[index].extension)) {
            return FORMATS[index].format;
        }
    }
    return TAG_FORMAT_UNKNOWN;
}

bool tag_read(const TagReader *reader, uint32_t offset, void *buffer, uint32_t length)
{
    if (offset > reader->size || length > reader->size - offset) {
        return false;
    }
    return length == 0 || reader->source.read(reader->source.context, offset, buffer, length);
}

uint16_t tag_le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

uint32_t tag_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

uint64_t tag_le64(const uint8_t *bytes)
{
    return (uint64_t)tag_le32(bytes) | (uint64_t)tag_le32(bytes + 4) << 32;
}

uint16_t tag_be16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

uint32_t tag_be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 |
           (uint32_t)bytes[3];
}

uint64_t tag_be64(const uint8_t *bytes)
{
    return (uint64_t)tag_be32(bytes) << 32 | (uint64_t)tag_be32(bytes + 4);
}

bool tag_same_text(const char *text, uint32_t length, const char *wanted)
{
    uint32_t index = 0;
    for (; index < length && wanted[index] != '\0'; index++) {
        if (text[index] != wanted[index]) {
            return false;
        }
    }
    return index == length && wanted[index] == '\0';
}

bool tag_same_text_folded(const char *text, uint32_t length, const char *wanted)
{
    uint32_t index = 0;
    for (; index < length && wanted[index] != '\0'; index++) {
        if (lower(text[index]) != wanted[index]) {
            return false;
        }
    }
    return index == length && wanted[index] == '\0';
}

void tag_mark_present(TagReader *reader, TagField field)
{
    reader->present[field] = true;
}

static bool is_space(uint32_t code_point)
{
    return code_point == ' ' || (code_point >= 0x09 && code_point <= 0x0D) ||
           (code_point >= 0x1C && code_point <= 0x1F) || code_point == 0x85 || code_point == 0xA0 ||
           code_point == 0x1680 || (code_point >= 0x2000 && code_point <= 0x200A) ||
           code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202F ||
           code_point == 0x205F || code_point == 0x3000;
}

typedef struct TextBuilder {
    char *text;
    uint32_t length;
    uint32_t content_end;
    bool started;
} TextBuilder;

static uint32_t utf8_size(uint32_t code_point)
{
    if (code_point < 0x80) {
        return 1;
    }
    if (code_point < 0x800) {
        return 2;
    }
    return code_point < 0x10000 ? 3 : 4;
}

static void put_code_point(TextBuilder *builder, uint32_t code_point)
{
    static const uint8_t LEADS[5] = {0, 0x00, 0xC0, 0xE0, 0xF0};
    if (code_point == 0 || (!builder->started && is_space(code_point))) {
        return;
    }
    bool space = is_space(code_point);
    uint32_t size = utf8_size(code_point);
    if (builder->length + size >= TRACK_TAG_TEXT_CAPACITY) {
        return;
    }
    for (uint32_t index = size - 1; index > 0; index--) {
        builder->text[builder->length + index] = (char)(0x80 | (code_point & 0x3F));
        code_point >>= 6;
    }
    builder->text[builder->length] = (char)(LEADS[size] | code_point);
    builder->length += size;
    builder->started = true;
    if (!space) {
        builder->content_end = builder->length;
    }
}

static uint32_t next_utf8(const uint8_t *text, uint32_t length, uint32_t *index)
{
    uint32_t lead = text[(*index)++];
    if (lead < 0x80) {
        return lead;
    }
    uint32_t extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    uint32_t code_point = lead & (0x3Fu >> extra);
    for (uint32_t count = 0; count < extra && *index < length; count++) {
        code_point = code_point << 6 | (text[(*index)++] & 0x3Fu);
    }
    return extra == 0 ? 0xFFFD : code_point;
}

static uint32_t next_utf16(const uint8_t *text, uint32_t length, uint32_t *index, bool big_endian)
{
    uint32_t unit = big_endian ? tag_be16(text + *index) : tag_le16(text + *index);
    *index += 2;
    if (unit >= 0xD800 && unit < 0xDC00 && *index + 1 < length) {
        uint32_t low = big_endian ? tag_be16(text + *index) : tag_le16(text + *index);
        if (low >= 0xDC00 && low < 0xE000) {
            *index += 2;
            return 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
        }
    }
    return unit;
}

static uint32_t next_code_point(const uint8_t *text, uint32_t length, uint32_t *index,
                                TagEncoding encoding)
{
    switch (encoding) {
    case TAG_LATIN1:
        return text[(*index)++];
    case TAG_UTF8:
        return next_utf8(text, length, index);
    case TAG_UTF16LE:
    case TAG_UTF16BE:
        if (*index + 1 >= length) {
            *index = length;
            return 0;
        }
        return next_utf16(text, length, index, encoding == TAG_UTF16BE);
    }
    return 0;
}

void tag_decode(char *out, const uint8_t *text, uint32_t length, TagEncoding encoding)
{
    TextBuilder builder = {.text = out, .length = 0, .content_end = 0, .started = false};
    for (uint32_t index = 0; index < length;) {
        put_code_point(&builder, next_code_point(text, length, &index, encoding));
    }
    out[builder.content_end] = '\0';
}

void tag_offer_decoded(TagReader *reader, TagField field, const char *text)
{
    reader->present[field] = true;
    char *target = reader->tags->text[field];
    if (target[0] != '\0') {
        return;
    }
    uint32_t index = 0;
    for (; text[index] != '\0' && index + 1 < TRACK_TAG_TEXT_CAPACITY; index++) {
        target[index] = text[index];
    }
    target[index] = '\0';
}

void tag_offer(TagReader *reader, TagField field, const uint8_t *text, uint32_t length,
               TagEncoding encoding)
{
    reader->present[field] = true;
    if (reader->tags->text[field][0] == '\0') {
        tag_decode(reader->tags->text[field], text, length, encoding);
    }
}

void tag_offer_picture(TagReader *reader, TagPicture picture)
{
    TagPicture *current = &reader->tags->picture;
    if (current->kind == TAG_PICTURE_NONE || (picture.front && !current->front)) {
        *current = picture;
    }
}

static bool read_format(TagReader *reader, TagFormat format)
{
    switch (format) {
    case TAG_FORMAT_FLAC:
        return tags_read_flac(reader);
    case TAG_FORMAT_OGG:
        return tags_read_ogg(reader);
    case TAG_FORMAT_MP3:
        return tags_read_mp3(reader);
    case TAG_FORMAT_MP4:
        return tags_read_mp4(reader);
    case TAG_FORMAT_AIFF:
        return tags_read_aiff(reader);
    case TAG_FORMAT_WAV:
        return tags_read_wav(reader);
    case TAG_FORMAT_WMA:
        return tags_read_wma(reader);
    case TAG_FORMAT_APE:
        return tags_read_ape(reader);
    case TAG_FORMAT_DSF:
        return tags_read_dsf(reader);
    case TAG_FORMAT_DFF:
        return tags_read_dff(reader);
    case TAG_FORMAT_UNKNOWN:
        return false;
    }
    return false;
}

static bool empty_gain_tail(const uint8_t *text, uint32_t length, uint32_t at)
{
    while (at < length) {
        if (text[at++] != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t gain_bom(const uint8_t *text, uint32_t length, TagEncoding *encoding)
{
    if (length < 2 || (*encoding != TAG_UTF16LE && *encoding != TAG_UTF16BE)) {
        return 0;
    }
    if (tag_be16(text) == 0xFEFF || tag_be16(text) == 0xFFFE) {
        *encoding = tag_be16(text) == 0xFEFF ? TAG_UTF16BE : TAG_UTF16LE;
        return 2;
    }
    return 0;
}

bool tag_decode_gain_text(char *out, const uint8_t *text, uint32_t length, TagEncoding encoding)
{
    uint32_t width = encoding == TAG_UTF16LE || encoding == TAG_UTF16BE ? 2 : 1;
    if (length % width != 0) {
        return false;
    }
    uint32_t count = 0;
    for (uint32_t at = gain_bom(text, length, &encoding); at < length; at += width) {
        uint32_t value = width == 1                ? text[at]
                         : encoding == TAG_UTF16BE ? tag_be16(text + at)
                                                   : tag_le16(text + at);
        if (value == 0) {
            if (!empty_gain_tail(text, length, at)) {
                return false;
            }
            break;
        }
        if (count + 1 >= REPLAY_GAIN_TEXT_CAPACITY || value > 127 ||
            (value < 32 && (value < 9 || value > 13))) {
            return false;
        }
        out[count++] = (char)value;
    }
    out[count] = '\0';
    return true;
}

void tag_offer_replay_gain(TagReader *reader, ReplayGainField field, const char *text)
{
    uint32_t length = 0;
    while (length < TRACK_TAG_TEXT_CAPACITY && text[length] != '\0') {
        length++;
    }
    (void)replay_gain_offer(&reader->replay_gain, field, text, length);
}

bool track_tags_read(ByteSource source, uint32_t size, TagFormat format, TrackTags *tags)
{
    *tags = (TrackTags){.duration_ms = 0, .picture = {.kind = TAG_PICTURE_NONE}};
    TagReader reader = {.source = source, .size = size, .tags = tags};
    bool read = read_format(&reader, format);
    if (read) {
        tags->replay_gain = replay_gain_finish(&reader.replay_gain);
    }
    return read;
}
