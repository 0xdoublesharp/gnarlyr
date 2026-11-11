#include "image/image_reader.h"

#include <stddef.h>

bool image_reader_init(ImageReader *reader, ImageSource source, ImageArena *arena)
{
    *reader = (ImageReader){.source = source, .capacity = IMAGE_READER_BUFFER, .failed = false};
    reader->buffer = image_arena_take(arena, IMAGE_READER_BUFFER);
    return reader->buffer != NULL;
}

bool image_reader_grow(ImageReader *reader, ImageArena *arena, uint32_t capacity)
{
    bool last = reader->buffer + IMAGE_READER_BUFFER == arena->base + arena->used;
    uint32_t extra = capacity - reader->capacity;
    if (!last || capacity <= reader->capacity || image_arena_take(arena, extra) == NULL) {
        return false;
    }
    reader->capacity = capacity;
    return true;
}

static void keep_unread(ImageReader *reader, uint32_t kept)
{
    uint32_t start =
        (reader->source.offset + reader->position + reader->cursor) % IMAGE_READER_ALIGNMENT;
    uint8_t *buffer = reader->buffer;
    uint32_t from = reader->cursor;
    for (uint32_t index = 0; start <= from && index < kept; index++) {
        buffer[start + index] = buffer[from + index];
    }
    for (uint32_t index = kept; start > from && index > 0; index--) {
        buffer[start + index - 1] = buffer[from + index - 1];
    }
    reader->position += from - start;
    reader->cursor = start;
    reader->filled = start + kept;
}

static uint32_t remaining_in_source(const ImageReader *reader)
{
    uint32_t consumed = reader->position + reader->filled;
    return consumed < reader->source.length ? reader->source.length - consumed : 0;
}

static bool refill(ImageReader *reader)
{
    uint32_t kept = reader->filled - reader->cursor;
    keep_unread(reader, kept);
    uint32_t room = reader->capacity - reader->filled;
    uint32_t left = remaining_in_source(reader);
    uint32_t count = room < left ? room : left;
    if (count == 0) {
        return false;
    }
    uint32_t offset = reader->source.offset + reader->position + reader->filled;
    if (!reader->source.read(reader->source.context, offset, reader->buffer + reader->filled,
                             count)) {
        reader->failed = true;
        return false;
    }
    reader->filled += count;
    return true;
}

bool image_reader_available(ImageReader *reader, uint32_t count)
{
    while (!reader->failed && reader->filled - reader->cursor < count) {
        if (!refill(reader)) {
            return false;
        }
    }
    return !reader->failed;
}

uint8_t image_reader_peek(ImageReader *reader, uint32_t ahead)
{
    if (!image_reader_available(reader, ahead + 1)) {
        return 0;
    }
    return reader->buffer[reader->cursor + ahead];
}

uint8_t image_reader_byte(ImageReader *reader)
{
    if (!image_reader_available(reader, 1)) {
        reader->failed = true;
        return 0;
    }
    return reader->buffer[reader->cursor++];
}

uint32_t image_reader_be16(ImageReader *reader)
{
    uint32_t high = image_reader_byte(reader);
    return high << 8 | image_reader_byte(reader);
}

uint32_t image_reader_be32(ImageReader *reader)
{
    uint32_t high = image_reader_be16(reader);
    return high << 16 | image_reader_be16(reader);
}

bool image_reader_read(ImageReader *reader, uint8_t *out, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        out[index] = image_reader_byte(reader);
    }
    return !reader->failed;
}

bool image_reader_skip(ImageReader *reader, uint32_t count)
{
    while (count > 0 && !reader->failed) {
        uint32_t buffered = reader->filled - reader->cursor;
        if (buffered == 0) {
            uint32_t left = remaining_in_source(reader);
            if (count > left) {
                reader->failed = true;
                return false;
            }
            reader->position += reader->filled + count;
            reader->filled = 0;
            reader->cursor = 0;
            return true;
        }
        uint32_t step = buffered < count ? buffered : count;
        reader->cursor += step;
        count -= step;
    }
    return !reader->failed;
}

bool image_reader_at_end(ImageReader *reader)
{
    return reader->filled == reader->cursor && remaining_in_source(reader) == 0;
}
