#include <stddef.h>

#include "tags_internal.h"

enum {
    KEY_CAPACITY = 32,
    VALUE_CAPACITY = 1024,
    BLOCK_STREAMINFO = 0,
    BLOCK_VORBIS_COMMENT = 4,
    BLOCK_PICTURE = 6,
    FRONT_COVER = 3,
    ID3_HEADER_SIZE = 10,
    OGG_HEADER_SIZE = 27,
    VORBIS_PACKET_PREFIX = 7,
    OPUS_PACKET_PREFIX = 8,
    OPUS_HEAD_SIZE = 19,
    OPUS_RATE = 48000
};

typedef struct VorbisKey {
    const char *name;
    TagField field;
} VorbisKey;

static const VorbisKey KEYS[] = {
    {"title", TAG_TITLE},     {"artist", TAG_ARTIST},     {"albumartist", TAG_ALBUM_ARTIST},
    {"album", TAG_ALBUM},     {"genre", TAG_GENRE},       {"date", TAG_DATE},
    {"discnumber", TAG_DISC}, {"tracknumber", TAG_TRACK},
};

static const char PICTURE_KEY[] = "metadata_block_picture";

static bool source_read(ByteSource source, uint32_t offset, void *buffer, uint32_t length)
{
    return source.read(source.context, offset, buffer, length);
}

static uint32_t key_length(const uint8_t *entry, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (entry[index] == '=') {
            return index;
        }
    }
    return length;
}

static int field_for_key(const uint8_t *key, uint32_t length)
{
    for (size_t index = 0; index < sizeof KEYS / sizeof KEYS[0]; index++) {
        if (tag_same_text_folded((const char *)key, length, KEYS[index].name)) {
            return (int)KEYS[index].field;
        }
    }
    return -1;
}

static int base64_value(uint8_t character)
{
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    return character == '+' ? 62 : character == '/' ? 63 : -1;
}

static bool base64_picture_is_front(const uint8_t *text, uint32_t length)
{
    if (length < 8) {
        return false;
    }
    uint32_t bits = 0;
    for (int index = 0; index < 6; index++) {
        int value = base64_value(text[index]);
        if (value < 0) {
            return false;
        }
        bits = bits << 6 | (uint32_t)value;
    }
    return (bits >> 4) == FRONT_COVER;
}

static void offer_entry(TagReader *reader, ByteSource source, uint32_t offset, uint32_t length)
{
    uint8_t value[VALUE_CAPACITY];
    uint32_t prefix = length < KEY_CAPACITY ? length : KEY_CAPACITY;
    if (!source_read(source, offset, value, prefix)) {
        return;
    }
    uint32_t key = key_length(value, prefix);
    if (key == prefix) {
        return;
    }
    uint32_t value_offset = offset + key + 1;
    uint32_t value_length = length - key - 1;
    if (tag_same_text_folded((const char *)value, key, PICTURE_KEY)) {
        uint32_t head = value_length < 8 ? value_length : 8;
        if (source_read(source, value_offset, value, head)) {
            tag_offer_picture(reader, (TagPicture){.kind = TAG_PICTURE_BASE64_FLAC_BLOCK,
                                                   .front = base64_picture_is_front(value, head),
                                                   .offset = value_offset,
                                                   .length = value_length});
        }
        return;
    }
    ReplayGainField gain = replay_gain_field((const char *)value, key);
    if (gain != REPLAY_GAIN_NO_FIELD) {
        if (value_length <= sizeof value &&
            source_read(source, value_offset, value, value_length)) {
            (void)replay_gain_offer(&reader->replay_gain, gain, (const char *)value, value_length);
        }
        return;
    }
    int field = field_for_key(value, key);
    if (field < 0) {
        return;
    }
    uint32_t wanted = value_length < VALUE_CAPACITY ? value_length : VALUE_CAPACITY;
    if (source_read(source, value_offset, value, wanted)) {
        tag_offer(reader, (TagField)field, value, wanted, TAG_UTF8);
    }
}

bool tags_read_vorbis_comment(TagReader *reader, ByteSource source, uint32_t offset, uint32_t limit)
{
    uint8_t word[4];
    if (!source_read(source, offset, word, 4)) {
        return false;
    }
    uint32_t cursor = offset + 4 + tag_le32(word);
    if (cursor < offset || !source_read(source, cursor, word, 4)) {
        return false;
    }
    uint32_t count = tag_le32(word);
    cursor += 4;
    for (uint32_t entry = 0; entry < count; entry++) {
        if (cursor > limit - 4 || !source_read(source, cursor, word, 4)) {
            return false;
        }
        uint32_t length = tag_le32(word);
        cursor += 4;
        if (length > limit - cursor) {
            return false;
        }
        offer_entry(reader, source, cursor, length);
        cursor += length;
    }
    return true;
}

static bool read_file_bytes(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    return tag_read(context, offset, buffer, length);
}

static ByteSource file_source(TagReader *reader)
{
    return (ByteSource){.read = read_file_bytes, .context = reader};
}

static uint32_t synchsafe(const uint8_t *bytes)
{
    return (uint32_t)(bytes[0] & 0x7F) << 21 | (uint32_t)(bytes[1] & 0x7F) << 14 |
           (uint32_t)(bytes[2] & 0x7F) << 7 | (uint32_t)(bytes[3] & 0x7F);
}

static uint32_t skip_id3(const TagReader *reader)
{
    uint32_t offset = 0;
    uint8_t header[ID3_HEADER_SIZE];
    while (tag_read(reader, offset, header, sizeof header) && header[0] == 'I' &&
           header[1] == 'D' && header[2] == '3') {
        offset += ID3_HEADER_SIZE + synchsafe(header + 6);
    }
    return offset;
}

static void read_streaminfo(TagReader *reader, const uint8_t *info)
{
    uint32_t rate = (uint32_t)info[10] << 12 | (uint32_t)info[11] << 4 | info[12] >> 4;
    uint64_t samples = (uint64_t)(info[13] & 0x0F) << 32 | tag_be32(info + 14);
    if (rate > 0) {
        tag_set_duration(reader, (double)samples / (double)rate);
    }
}

static void read_flac_picture(TagReader *reader, uint32_t offset, uint32_t end)
{
    uint8_t word[4];
    if (!tag_read(reader, offset, word, 4)) {
        return;
    }
    bool front = tag_be32(word) == FRONT_COVER;
    uint32_t cursor = offset + 4;
    for (int skipped = 0; skipped < 2; skipped++) {
        if (!tag_read(reader, cursor, word, 4)) {
            return;
        }
        cursor += 4 + tag_be32(word);
    }
    cursor += 16;
    if (cursor > end - 4 || !tag_read(reader, cursor, word, 4)) {
        return;
    }
    uint32_t length = tag_be32(word);
    if (length <= end - cursor - 4) {
        tag_offer_picture(reader, (TagPicture){.kind = TAG_PICTURE_RAW,
                                               .front = front,
                                               .offset = cursor + 4,
                                               .length = length});
    }
}

bool tags_read_flac(TagReader *reader)
{
    uint32_t offset = skip_id3(reader);
    uint8_t header[34];
    if (!tag_read(reader, offset, header, 4) || !tag_same_text((const char *)header, 4, "fLaC")) {
        return false;
    }
    offset += 4;
    bool found_info = false;
    bool last = false;
    while (!last && tag_read(reader, offset, header, 4)) {
        last = (header[0] & 0x80) != 0;
        uint32_t type = header[0] & 0x7F;
        uint32_t length = (uint32_t)header[1] << 16 | (uint32_t)header[2] << 8 | header[3];
        uint32_t body = offset + 4;
        if (length > reader->size - body) {
            return false;
        }
        if (type == BLOCK_STREAMINFO && length >= sizeof header &&
            tag_read(reader, body, header, 34)) {
            read_streaminfo(reader, header);
            found_info = true;
        } else if (type == BLOCK_VORBIS_COMMENT) {
            tags_read_vorbis_comment(reader, file_source(reader), body, body + length);
        } else if (type == BLOCK_PICTURE) {
            read_flac_picture(reader, body, body + length);
        }
        offset = body + length;
    }
    return found_info;
}

typedef struct OggPage {
    uint32_t offset;
    uint32_t header_length;
    uint32_t body_length;
    uint32_t serial;
    uint64_t granule;
    uint8_t flags;
} OggPage;

static bool read_page(const TagReader *reader, uint32_t offset, OggPage *page)
{
    uint8_t header[OGG_HEADER_SIZE];
    uint8_t segments[255];
    if (!tag_read(reader, offset, header, sizeof header) ||
        !tag_same_text((const char *)header, 4, "OggS") || header[4] != 0 ||
        (header[5] & ~7u) != 0) {
        return false;
    }
    uint32_t count = header[26];
    if (!tag_read(reader, offset + OGG_HEADER_SIZE, segments, count)) {
        return false;
    }
    uint32_t body = 0;
    for (uint32_t index = 0; index < count; index++) {
        body += segments[index];
    }
    uint32_t header_length = OGG_HEADER_SIZE + count;
    if (body > reader->size - offset - header_length ||
        ((header[5] & 4) != 0 && count != 0 && segments[count - 1] == 255)) {
        return false;
    }
    *page = (OggPage){.offset = offset,
                      .header_length = header_length,
                      .body_length = body,
                      .serial = tag_le32(header + 14),
                      .granule = tag_le64(header + 6),
                      .flags = header[5]};
    return true;
}

static uint32_t page_end(const OggPage *page)
{
    return page->offset + page->header_length + page->body_length;
}

static bool read_serial_page(const TagReader *reader, uint32_t offset, uint32_t serial,
                             OggPage *page)
{
    while (offset < reader->size) {
        if (!read_page(reader, offset, page)) {
            return false;
        }
        if (page->serial == serial) {
            return true;
        }
        offset = page_end(page);
    }
    return false;
}

typedef struct OggStream {
    TagReader *reader;
    OggPage page;
    uint32_t page_start;
    uint32_t serial;
} OggStream;

static bool stream_seek(OggStream *stream, uint32_t offset)
{
    while (offset >= stream->page_start + stream->page.body_length) {
        if ((stream->page.flags & 4) != 0) {
            return false;
        }
        uint32_t next = page_end(&stream->page);
        stream->page_start += stream->page.body_length;
        if (!read_serial_page(stream->reader, next, stream->serial, &stream->page) ||
            (stream->page.flags & 2) != 0) {
            return false;
        }
    }
    return true;
}

static bool read_stream_bytes(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    OggStream *stream = context;
    uint8_t *out = buffer;
    if (offset < stream->page_start) {
        return false;
    }
    while (length > 0) {
        if (!stream_seek(stream, offset)) {
            return false;
        }
        uint32_t within = offset - stream->page_start;
        uint32_t chunk = stream->page.body_length - within;
        chunk = chunk < length ? chunk : length;
        uint32_t file_offset = stream->page.offset + stream->page.header_length + within;
        if (!tag_read(stream->reader, file_offset, out, chunk)) {
            return false;
        }
        out += chunk;
        offset += chunk;
        length -= chunk;
    }
    return true;
}

static bool first_eos_granule(const TagReader *reader, const OggPage *first, uint64_t *granule)
{
    OggPage page = *first;
    while ((page.flags & 4) == 0) {
        if (!read_serial_page(reader, page_end(&page), first->serial, &page) ||
            (page.flags & 2) != 0) {
            return false;
        }
    }
    if (page.granule == UINT64_MAX) {
        return false;
    }
    *granule = page.granule;
    return true;
}

typedef struct OggCodec {
    uint32_t rate;
    uint32_t pre_skip;
    uint32_t prefix;
    const char *comment;
} OggCodec;

static bool ogg_codec(const uint8_t *head, OggCodec *codec)
{
    if (head[0] == 1 && tag_same_text((const char *)head + 1, 6, "vorbis")) {
        *codec = (OggCodec){tag_le32(head + 12), 0, VORBIS_PACKET_PREFIX, "\3vorbis"};
        return true;
    }
    if (tag_same_text((const char *)head, 8, "OpusHead")) {
        *codec = (OggCodec){OPUS_RATE, tag_le16(head + 10), OPUS_PACKET_PREFIX, "OpusTags"};
        return true;
    }
    return false;
}

bool tags_read_ogg(TagReader *reader)
{
    OggPage first;
    uint8_t identification[OPUS_HEAD_SIZE];
    OggCodec codec;
    if (!read_page(reader, 0, &first) || first.body_length < sizeof identification ||
        !tag_read(reader, first.header_length, identification, sizeof identification) ||
        !ogg_codec(identification, &codec)) {
        return false;
    }
    OggStream stream = {.reader = reader, .page_start = 0, .serial = first.serial};
    if ((first.flags & 4) != 0 ||
        !read_serial_page(reader, page_end(&first), first.serial, &stream.page) ||
        (stream.page.flags & 2) != 0) {
        return false;
    }
    ByteSource source = {.read = read_stream_bytes, .context = &stream};
    uint8_t prefix[OPUS_PACKET_PREFIX];
    if (source_read(source, 0, prefix, codec.prefix) &&
        tag_same_text((const char *)prefix, codec.prefix, codec.comment)) {
        tags_read_vorbis_comment(reader, source, codec.prefix, UINT32_MAX);
    }
    uint64_t granule = 0;
    if (codec.rate > 0 && first_eos_granule(reader, &first, &granule) &&
        granule >= codec.pre_skip) {
        tag_set_duration(reader, (double)(granule - codec.pre_skip) / (double)codec.rate);
    }
    return true;
}
