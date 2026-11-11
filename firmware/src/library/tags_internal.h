#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "image/image_reader.h"
#include "library/replay_gain.h"
#include "library/track_tags.h"

typedef struct TagUnsync {
    ByteSource source;
    uint32_t base;
    uint32_t end;
    uint32_t raw;
    uint32_t decoded;
    bool after_ff;
} TagUnsync;

typedef enum TagEncoding {
    TAG_LATIN1,
    TAG_UTF8,
    TAG_UTF16LE,
    TAG_UTF16BE
} TagEncoding;

typedef struct TagReader {
    ByteSource source;
    uint32_t size;
    TrackTags *tags;
    bool present[TAG_FIELD_COUNT];
    ReplayGainTags replay_gain;
    bool opus;
    bool comment_pictures;
    uint32_t comment_page;
    const TagUnsync *unsync;
} TagReader;

typedef struct OggPage {
    uint32_t offset;
    uint32_t header_length;
    uint32_t body_length;
    uint32_t serial;
    uint64_t granule;
    uint8_t flags;
} OggPage;

typedef struct OggStream {
    TagReader *reader;
    OggPage page;
    uint32_t page_start;
    uint32_t serial;
} OggStream;

typedef struct TagPictureReader {
    TagReader file;
    TagUnsync unsync;
    OggStream ogg;
    uint32_t first_page;
    uint32_t text;
    uint32_t text_length;
} TagPictureReader;

typedef struct Mp4Atom {
    uint32_t offset;
    uint32_t body;
    uint32_t end;
    char type[4];
} Mp4Atom;

bool tag_read(const TagReader *reader, uint32_t offset, void *buffer, uint32_t length);
uint16_t tag_le16(const uint8_t *bytes);
uint32_t tag_le32(const uint8_t *bytes);
uint64_t tag_le64(const uint8_t *bytes);
uint16_t tag_be16(const uint8_t *bytes);
uint32_t tag_be32(const uint8_t *bytes);
uint64_t tag_be64(const uint8_t *bytes);
bool tag_same_text(const char *text, uint32_t length, const char *wanted);
bool tag_same_text_folded(const char *text, uint32_t length, const char *wanted);
void tag_mark_present(TagReader *reader, TagField field);
void tag_decode(char *out, const uint8_t *text, uint32_t length, TagEncoding encoding);
void tag_offer(TagReader *reader, TagField field, const uint8_t *text, uint32_t length,
               TagEncoding encoding);
void tag_offer_decoded(TagReader *reader, TagField field, const char *text);
void tag_offer_picture(TagReader *reader, TagPicture picture);
void tag_offer_replay_gain(TagReader *reader, ReplayGainField field, const char *text);
bool tag_decode_gain_text(char *out, const uint8_t *text, uint32_t length, TagEncoding encoding);
void tag_set_duration(TagReader *reader, double seconds);

void tag_unsync_start(TagUnsync *view, ByteSource source, uint32_t base, uint32_t end);
bool tag_unsync_read(void *context, uint32_t offset, void *buffer, uint32_t length);
int tags_base64_value(uint8_t character);
bool tags_ogg_stream_open(OggStream *stream, TagReader *reader, uint32_t offset);
bool tags_ogg_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length);
bool tag_picture_open(TagPictureReader *reader, ByteSource source, uint32_t size,
                      const TagPicture *picture, ImageSource *image);

bool tags_read_flac(TagReader *reader);
bool tags_read_ogg(TagReader *reader);
bool tags_read_vorbis_comment(TagReader *reader, ByteSource source, uint32_t offset,
                              uint32_t limit);
bool tags_read_id3v2(TagReader *reader, uint32_t offset, uint32_t *tag_size);
void tags_read_id3v1(TagReader *reader);
const char *tags_id3_genre(uint32_t index);
bool tags_read_mp3(TagReader *reader);
bool tags_read_mp4(TagReader *reader);
bool tags_mp4_read_atom(const TagReader *reader, uint32_t offset, uint32_t limit, Mp4Atom *atom);
bool tags_mp4_find_child(const TagReader *reader, const Mp4Atom *parent, uint32_t skip,
                         const char *type, Mp4Atom *found);
bool tags_read_wav(TagReader *reader);
bool tags_read_dsf(TagReader *reader);
bool tags_read_dff(TagReader *reader);
bool tags_read_ape(TagReader *reader);
void tags_read_apev2(TagReader *reader);
bool tags_read_wma(TagReader *reader);
