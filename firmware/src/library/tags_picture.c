#include <stddef.h>

#include "tags_internal.h"

enum {
    UNSYNC_CHUNK = 64,
    BASE64_CHUNK = 64,
    GROUP_TEXT = 4,
    GROUP_BYTES = 3,
    PICTURE_SIZES = 16,
    BYTE_FF = 0xFF
};

void tag_unsync_start(TagUnsync *view, ByteSource source, uint32_t base, uint32_t end)
{
    *view = (TagUnsync){.source = source, .base = base, .end = end, .raw = base};
}

static void unsync_rewind(TagUnsync *view)
{
    view->raw = view->base;
    view->decoded = 0;
    view->after_ff = false;
}

static uint32_t unsync_take(TagUnsync *view, const uint8_t *chunk, uint32_t count, uint32_t *target,
                            uint8_t **out, uint32_t *length)
{
    uint32_t index = 0;
    for (; index < count && *length > 0; index++) {
        uint8_t byte = chunk[index];
        bool dropped = view->after_ff && byte == 0;
        view->after_ff = !dropped && byte == BYTE_FF;
        if (dropped) {
            continue;
        }
        if (view->decoded++ == *target) {
            *(*out)++ = byte;
            (*target)++;
            (*length)--;
        }
    }
    return index;
}

bool tag_unsync_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    TagUnsync *view = context;
    uint8_t *out = buffer;
    uint8_t chunk[UNSYNC_CHUNK];
    if (offset < view->base) {
        return false;
    }
    uint32_t target = offset - view->base;
    if (target < view->decoded) {
        unsync_rewind(view);
    }
    while (length > 0) {
        uint32_t left = view->end - view->raw;
        uint32_t count = left < UNSYNC_CHUNK ? left : UNSYNC_CHUNK;
        if (count == 0) {
            for (; length > 0; length--) {
                *out++ = 0;
            }
            return true;
        }
        if (!view->source.read(view->source.context, view->raw, chunk, count)) {
            return false;
        }
        view->raw += unsync_take(view, chunk, count, &target, &out, &length);
    }
    return true;
}

static uint32_t decode_group(const uint8_t *text, bool *valid)
{
    uint32_t bits = 0;
    for (uint32_t index = 0; index < GROUP_TEXT; index++) {
        int value = tags_base64_value(text[index]);
        *valid = *valid && (value >= 0 || text[index] == '=');
        bits = bits << 6 | (uint32_t)(value < 0 ? 0 : value);
    }
    return bits;
}

static bool base64_chunk(TagPictureReader *reader, uint32_t group, uint8_t *text, uint32_t *count)
{
    uint32_t at = group * GROUP_TEXT;
    uint32_t left = at < reader->text_length ? reader->text_length - at : 0;
    *count = (left < BASE64_CHUNK ? left : BASE64_CHUNK) / GROUP_TEXT * GROUP_TEXT;
    uint32_t stream_at = reader->text + at;
    if (stream_at < reader->ogg.page_start &&
        !tags_ogg_stream_open(&reader->ogg, &reader->file, reader->first_page)) {
        return false;
    }
    return *count > 0 && tags_ogg_stream_read(&reader->ogg, stream_at, text, *count);
}

static bool base64_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    TagPictureReader *reader = context;
    uint8_t *out = buffer;
    uint8_t text[BASE64_CHUNK];
    bool valid = true;
    while (length > 0 && valid) {
        uint32_t skip = offset % GROUP_BYTES;
        uint32_t count = 0;
        if (!base64_chunk(reader, offset / GROUP_BYTES, text, &count)) {
            return false;
        }
        for (uint32_t at = 0; at < count && length > 0; at += GROUP_TEXT) {
            uint32_t bits = decode_group(text + at, &valid);
            for (uint32_t byte = skip; byte < GROUP_BYTES && length > 0; byte++, length--) {
                *out++ = (uint8_t)(bits >> (16 - 8 * byte));
                offset++;
            }
            skip = 0;
        }
    }
    return valid;
}

static bool block_word(TagPictureReader *reader, uint32_t offset, uint32_t *value)
{
    uint8_t word[4];
    bool read = base64_read(reader, offset, word, sizeof word);
    *value = tag_be32(word);
    return read;
}

static bool open_base64(TagPictureReader *reader, const TagPicture *picture, ImageSource *image)
{
    uint32_t cursor = 4;
    uint32_t length = 0;
    reader->text = picture->offset;
    reader->text_length = picture->length;
    reader->first_page = picture->base;
    if (!tags_ogg_stream_open(&reader->ogg, &reader->file, picture->base)) {
        return false;
    }
    for (int field = 0; field < 2; field++) {
        if (!block_word(reader, cursor, &length) || length > picture->length) {
            return false;
        }
        cursor += 4 + length;
    }
    cursor += PICTURE_SIZES;
    if (!block_word(reader, cursor, &length)) {
        return false;
    }
    cursor += 4;
    uint32_t room = picture->length / GROUP_TEXT * GROUP_BYTES;
    *image = (ImageSource){base64_read, reader, cursor, length};
    return cursor <= room && length <= room - cursor;
}

bool tag_picture_open(TagPictureReader *reader, ByteSource source, uint32_t size,
                      const TagPicture *picture, ImageSource *image)
{
    reader->file = (TagReader){.source = source, .size = size, .tags = NULL};
    if (picture->kind == TAG_PICTURE_BASE64_FLAC_BLOCK) {
        return open_base64(reader, picture, image);
    }
    *image = (ImageSource){source.read, source.context, picture->offset, picture->length};
    if (picture->kind == TAG_PICTURE_UNSYNC) {
        tag_unsync_start(&reader->unsync, source, picture->base, picture->end);
        *image = (ImageSource){tag_unsync_read, &reader->unsync, picture->offset, picture->length};
    }
    return picture->kind != TAG_PICTURE_NONE && picture->length > 0;
}
