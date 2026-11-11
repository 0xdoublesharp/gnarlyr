#include "device/mp4_demux.h"

#include "codec/alac.h"

#include <stddef.h>

enum {
    BOX_HEADER = 8,
    FULL_BOX = 4,
    FTYP = 0x66747970,
    MOOV = 0x6D6F6F76,
    MVHD = 0x6D766864,
    MDHD = 0x6D646864,
    EDTS = 0x65647473,
    ELST = 0x656C7374,
    MDAT = 0x6D646174,
    TRAK = 0x7472616B,
    MDIA = 0x6D646961,
    HDLR = 0x68646C72,
    SOUN = 0x736F756E,
    MINF = 0x6D696E66,
    STBL = 0x7374626C,
    STSD = 0x73747364,
    STTS = 0x73747473,
    STSC = 0x73747363,
    STSZ = 0x7374737A,
    STCO = 0x7374636F,
    CO64 = 0x636F3634,
    MP4A = 0x6D703461,
    ALAC = 0x616C6163,
    ESDS = 0x65736473,
    UDTA = 0x75647461,
    META = 0x6D657461,
    ILST = 0x696C7374,
    FREEFORM = 0x2D2D2D2D,
    NAME = 0x6E616D65,
    DATA = 0x64617461,
    TABLE_COUNT = 5,
    TABLE_SIZES = 2,
    TABLE_RUNS = 3,
    TABLE_CHUNKS = 4,
    ENTRY_FIXED = 28,
    ENTRY_VERSION_AT = 8,
    ENTRY_VERSION_1_EXTRA = 16,
    STSD_ENTRY_AT = 8,
    ESDS_SKIP_ES = 3,
    ESDS_SKIP_DECODER = 13,
    TAG_ES = 3,
    TAG_DECODER = 4,
    TAG_CONFIG = 5,
    AAC_LC = 2,
    AAC_PLAIN_CONFIG = 2,
    AAC_SBR = 5,
    AAC_PS = 29,
    SBR_SYNC = 0x2B7,
    SAMPLE_RATES = 13,
    ALAC_COOKIE = 24,
    ALAC_COOKIE_RATE_AT = 20,
    STTS_SCAN_LIMIT = 64,
    GAPLESS_TEXT_BYTES = 96,
    GAPLESS_NAME_BYTES = 8,
    MAX_DEPTH = 2
};

static const char GAPLESS_NAME[] = "iTunSMPB";
static const uint32_t MOOV_PATH = MOOV;
static const uint32_t AAC_RATES[SAMPLE_RATES] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                                 22050, 16000, 12000, 11025, 8000,  7350};

typedef struct Mp4Box {
    uint32_t body;
    uint32_t end;
    uint32_t type;
} Mp4Box;

static uint32_t big_endian(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 |
           (uint32_t)bytes[3];
}

static bool read_bytes(FlacInput input, uint32_t offset, void *buffer, uint32_t length)
{
    return offset <= input.size && length <= input.size - offset &&
           input.read(input.context, offset, buffer, length);
}

static bool read_word(FlacInput input, uint32_t offset, uint32_t *value)
{
    uint8_t bytes[4];
    if (!read_bytes(input, offset, bytes, sizeof bytes)) {
        return false;
    }
    *value = big_endian(bytes);
    return true;
}

static bool read_box(FlacInput input, uint32_t at, uint32_t limit, Mp4Box *box)
{
    uint8_t header[BOX_HEADER];
    if (at > limit || limit - at < BOX_HEADER || !read_bytes(input, at, header, BOX_HEADER)) {
        return false;
    }
    uint32_t size = big_endian(header);
    size = size == 0 ? limit - at : size;
    if (size < BOX_HEADER || size > limit - at) {
        return false;
    }
    *box = (Mp4Box){.body = at + BOX_HEADER, .end = at + size, .type = big_endian(header + 4)};
    return true;
}

static bool find_box(FlacInput input, const Mp4Box *parent, uint32_t skip, uint32_t type,
                     Mp4Box *box)
{
    uint32_t at = parent->body + skip;
    while (at <= parent->end && read_box(input, at, parent->end, box)) {
        if (box->type == type) {
            return true;
        }
        at = box->end;
    }
    return false;
}

static bool is_sound(FlacInput input, const Mp4Box *mdia)
{
    Mp4Box handler;
    uint32_t kind;
    return find_box(input, mdia, 0, HDLR, &handler) && read_word(input, handler.body + 8, &kind) &&
           kind == SOUN;
}

static bool find_sound(FlacInput input, const Mp4Box *moov, Mp4Box *mdia, Mp4Box *selected)
{
    uint32_t at = moov->body;
    Mp4Box trak;
    while (at <= moov->end && read_box(input, at, moov->end, &trak)) {
        if (trak.type == TRAK && find_box(input, &trak, 0, MDIA, mdia) && is_sound(input, mdia)) {
            *selected = trak;
            return true;
        }
        at = trak.end;
    }
    return false;
}

static bool find_path(FlacInput input, Mp4Box *box, const uint32_t *path, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        Mp4Box next;
        if (!find_box(input, box, index > 0 && path[index - 1] == META ? FULL_BOX : 0, path[index],
                      &next)) {
            return false;
        }
        *box = next;
    }
    return true;
}

static bool find_in_entry(FlacInput input, const Mp4Box *entry, uint32_t type, Mp4Box *box)
{
    uint8_t version[2];
    if (!read_bytes(input, entry->body + ENTRY_VERSION_AT, version, sizeof version)) {
        return false;
    }
    uint32_t skip = ENTRY_FIXED + (version[1] == 1 ? ENTRY_VERSION_1_EXTRA : 0);
    return find_box(input, entry, skip, type, box);
}

static bool descriptor_header(FlacInput input, uint32_t *at, uint32_t *tag, uint32_t *length)
{
    uint8_t byte;
    if (!read_bytes(input, *at, &byte, 1)) {
        return false;
    }
    *tag = byte;
    *at += 1;
    uint32_t size = 0;
    for (uint32_t index = 0; index < 4; index++) {
        if (!read_bytes(input, *at, &byte, 1)) {
            return false;
        }
        *at += 1;
        size = size << 7 | (byte & 0x7Fu);
        if ((byte & 0x80u) == 0) {
            *length = size;
            return true;
        }
    }
    return false;
}

static bool read_config(FlacInput input, const Mp4Box *esds, Mp4Track *track)
{
    uint32_t at = esds->body + FULL_BOX;
    uint32_t limit = esds->end;
    uint32_t tag;
    uint32_t length;
    while (at < limit && descriptor_header(input, &at, &tag, &length)) {
        if (at > limit || length > limit - at) {
            return false;
        }
        if (tag == TAG_ES || tag == TAG_DECODER) {
            uint32_t skip = tag == TAG_ES ? ESDS_SKIP_ES : ESDS_SKIP_DECODER;
            if (length < skip) {
                return false;
            }
            limit = at + length;
            at += skip;
        } else if (tag == TAG_CONFIG) {
            track->config_size = (uint8_t)(length < MP4_CONFIG_BYTES ? length : MP4_CONFIG_BYTES);
            return read_bytes(input, at, track->config, track->config_size);
        } else {
            return false;
        }
    }
    return false;
}

static bool has_sbr(const uint8_t *asc, uint32_t size)
{
    if (size < 4 || ((uint32_t)asc[2] << 3 | asc[3] >> 5) != SBR_SYNC) {
        return false;
    }
    uint32_t extension = asc[3] & 31u;
    return extension == AAC_PS || (extension == AAC_SBR && size >= 5 && (asc[4] & 0x80u) != 0);
}

static bool decode_aac_config(Mp4Track *track)
{
    const uint8_t *asc = track->config;
    if (track->config_size < 2 || asc[0] >> 3 != AAC_LC || (asc[1] & 4u) != 0) {
        return false;
    }
    uint32_t index = (uint32_t)(asc[0] & 7) << 1 | asc[1] >> 7;
    uint32_t channels = (asc[1] >> 3) & 15u;
    if (index >= SAMPLE_RATES || channels < 1 || channels > 2 || has_sbr(asc, track->config_size)) {
        return false;
    }
    track->config_size = AAC_PLAIN_CONFIG;
    track->sample_rate = AAC_RATES[index];
    track->frame_samples = 1024;
    track->channels = (uint8_t)channels;
    track->bits = 0;
    return true;
}

static bool read_aac(FlacInput input, const Mp4Box *entry, Mp4Track *track)
{
    Mp4Box esds;
    track->codec = MP4_CODEC_AAC;
    return find_in_entry(input, entry, ESDS, &esds) && read_config(input, &esds, track) &&
           decode_aac_config(track);
}

static bool read_alac(FlacInput input, const Mp4Box *entry, Mp4Track *track)
{
    Mp4Box cookie;
    track->codec = MP4_CODEC_ALAC;
    if (!find_in_entry(input, entry, ALAC, &cookie) ||
        cookie.end - cookie.body != FULL_BOX + ALAC_COOKIE ||
        !read_bytes(input, cookie.body + FULL_BOX, track->config, ALAC_COOKIE)) {
        return false;
    }
    AlacConfig config = {.frame_length = big_endian(track->config),
                         .sample_rate = big_endian(track->config + ALAC_COOKIE_RATE_AT),
                         .bit_depth = track->config[5],
                         .kb = track->config[8],
                         .channels = track->config[9]};
    if (track->config[4] != 0 || !alac_config_supported(&config)) {
        return false;
    }
    track->config_size = ALAC_COOKIE;
    track->bits = config.bit_depth;
    track->channels = config.channels;
    track->sample_rate = config.sample_rate;
    track->frame_samples = config.frame_length;
    return true;
}

static bool read_codec(FlacInput input, const Mp4Box *stsd, Mp4Track *track)
{
    Mp4Box entry;
    if (!read_box(input, stsd->body + STSD_ENTRY_AT, stsd->end, &entry)) {
        return false;
    }
    if (entry.type == MP4A) {
        return read_aac(input, &entry, track);
    }
    return entry.type == ALAC && read_alac(input, &entry, track);
}

static bool sample_units(uint64_t time, uint32_t scale, uint32_t rate, uint32_t *samples)
{
    if (scale == 0 || rate == 0 || time / scale > UINT32_MAX / rate) {
        return false;
    }
    uint64_t value = time / scale * rate + time % scale * rate / scale;
    if (value > UINT32_MAX) {
        return false;
    }
    *samples = (uint32_t)value;
    return true;
}

static uint32_t time_scale(FlacInput input, const Mp4Box *parent, uint32_t type)
{
    Mp4Box box;
    uint32_t version;
    uint32_t scale;
    if (!find_box(input, parent, 0, type, &box) || box.end - box.body < 4 ||
        !read_word(input, box.body, &version) || (version >> 24) > 1) {
        return 0;
    }
    uint32_t offset = (version >> 24) == 0 ? 12 : 20;
    return box.end - box.body >= offset + 4 && read_word(input, box.body + offset, &scale) ? scale
                                                                                           : 0;
}

static bool read_timing(FlacInput input, const Mp4Box *stts, Mp4Track *track, uint32_t scale)
{
    uint32_t entries;
    uint32_t count;
    uint32_t delta;
    uint64_t duration = 0;
    uint32_t samples_left = track->sample_count;
    if (stts->end - stts->body < 8 || !read_word(input, stts->body + 4, &entries) || entries == 0 ||
        entries > STTS_SCAN_LIMIT || entries > (stts->end - stts->body - 8) / 8) {
        return false;
    }
    scale = scale == 0 ? track->sample_rate : scale;
    for (uint32_t index = 0; index < entries; index++) {
        if (!read_word(input, stts->body + 8 + 8 * index, &count) ||
            !read_word(input, stts->body + 12 + 8 * index, &delta) || delta == 0 ||
            count > samples_left || duration > UINT64_MAX - (uint64_t)count * delta) {
            return false;
        }
        duration += (uint64_t)count * delta;
        samples_left -= count;
    }
    return samples_left == 0 && track->frame_samples != 0 &&
           sample_units(duration, scale, track->sample_rate, &track->total_samples);
}

typedef struct Mp4Tables {
    Mp4Box box[TABLE_COUNT];
    uint32_t found;
} Mp4Tables;

static bool collect_tables(FlacInput input, const Mp4Box *stbl, Mp4Tables *tables)
{
    static const uint32_t TYPES[TABLE_COUNT] = {STSD, STTS, STSZ, STSC, STCO};
    Mp4Box child;
    uint32_t at = stbl->body;
    *tables = (Mp4Tables){0};
    while (read_box(input, at, stbl->end, &child)) {
        for (uint32_t index = 0; index < TABLE_COUNT; index++) {
            if (child.type == TYPES[index] || (index == TABLE_CHUNKS && child.type == CO64)) {
                tables->box[index] = child;
                tables->found |= 1u << index;
            }
        }
        at = child.end;
    }
    return tables->found == (1u << TABLE_COUNT) - 1;
}

static bool read_tables(FlacInput input, const Mp4Tables *tables, Mp4Track *track)
{
    const Mp4Box *sizes = &tables->box[TABLE_SIZES];
    const Mp4Box *runs = &tables->box[TABLE_RUNS];
    const Mp4Box *chunks = &tables->box[TABLE_CHUNKS];
    if (sizes->end - sizes->body < 12 || runs->end - runs->body < 8 ||
        chunks->end - chunks->body < 8) {
        return false;
    }
    track->wide_offsets = chunks->type == CO64;
    track->sizes_at = sizes->body + 12;
    track->runs_at = runs->body + 8;
    track->chunks_at = chunks->body + 8;
    return read_word(input, sizes->body + 4, &track->fixed_size) &&
           read_word(input, sizes->body + 8, &track->sample_count) &&
           read_word(input, runs->body + 4, &track->run_count) &&
           read_word(input, chunks->body + 4, &track->chunk_count) && track->run_count > 0 &&
           track->chunk_count > 0 &&
           (track->fixed_size != 0 || track->sample_count <= (sizes->end - track->sizes_at) / 4) &&
           track->run_count <= (runs->end - track->runs_at) / 12 &&
           track->chunk_count <= (chunks->end - track->chunks_at) / (track->wide_offsets ? 8 : 4);
}

static bool same_text(const uint8_t *bytes, const char *text)
{
    for (uint32_t index = 0; index < GAPLESS_NAME_BYTES; index++) {
        if (bytes[index] != (uint8_t)text[index]) {
            return false;
        }
    }
    return true;
}

static bool find_gapless_text(FlacInput input, const Mp4Box *moov, Mp4Box *text)
{
    static const uint32_t ILST_PATH[3] = {UDTA, META, ILST};
    Mp4Box ilst = *moov;
    Mp4Box item;
    if (!find_path(input, &ilst, ILST_PATH, 3)) {
        return false;
    }
    uint32_t at = ilst.body;
    while (read_box(input, at, ilst.end, &item)) {
        uint8_t name[GAPLESS_NAME_BYTES];
        Mp4Box label;
        if (item.type == FREEFORM && find_box(input, &item, 0, NAME, &label) &&
            read_bytes(input, label.body + FULL_BOX, name, sizeof name) &&
            same_text(name, GAPLESS_NAME)) {
            return find_box(input, &item, 0, DATA, text);
        }
        at = item.end;
    }
    return false;
}

static bool hex_value(const uint8_t *text, uint32_t length, uint32_t *at, uint32_t *value)
{
    while (*at < length && text[*at] == ' ') {
        (*at)++;
    }
    uint32_t first = *at;
    *value = 0;
    while (*at < length && text[*at] != ' ' && text[*at] != 0) {
        uint32_t byte = text[*at];
        uint32_t lower = byte | 32u;
        uint32_t digit = byte >= '0' && byte <= '9'     ? byte - '0'
                         : lower >= 'a' && lower <= 'f' ? lower - 'a' + 10
                                                        : 16;
        if (digit > 15 || *value > (UINT32_MAX - digit) / 16) {
            return false;
        }
        *value = *value << 4 | digit;
        (*at)++;
    }
    return *at > first;
}

static void read_gapless(FlacInput input, const Mp4Box *moov, Mp4Track *track)
{
    Mp4Box text;
    uint8_t bytes[GAPLESS_TEXT_BYTES];
    if (!find_gapless_text(input, moov, &text) || text.end - text.body <= 8) {
        return;
    }
    uint32_t length = text.end - text.body - 8;
    length = length < sizeof bytes ? length : (uint32_t)sizeof bytes;
    if (!read_bytes(input, text.body + 8, bytes, length)) {
        return;
    }
    uint32_t at = 0;
    uint32_t fields[4];
    for (uint32_t index = 0; index < 4; index++) {
        if (!hex_value(bytes, length, &at, &fields[index])) {
            return;
        }
    }
    if (fields[3] != 0 && fields[1] <= track->total_samples &&
        fields[3] <= track->total_samples - fields[1]) {
        track->delay = fields[1];
        track->valid_samples = fields[3];
    }
}

typedef struct EditRange {
    uint64_t duration;
    uint64_t media_time;
} EditRange;

static uint64_t big_wide(const uint8_t *bytes)
{
    return (uint64_t)big_endian(bytes) << 32 | big_endian(bytes + 4);
}

static bool read_edit(FlacInput input, const Mp4Box *trak, EditRange *edit)
{
    Mp4Box box;
    Mp4Box container;
    uint8_t bytes[28];
    if (!find_box(input, trak, 0, EDTS, &container) ||
        !find_box(input, &container, 0, ELST, &box) || box.end - box.body < 8 ||
        !read_bytes(input, box.body, bytes, 8) || bytes[0] > 1 || bytes[1] != 0 || bytes[2] != 0 ||
        bytes[3] != 0 || big_endian(bytes + 4) != 1) {
        return false;
    }
    uint32_t size = bytes[0] == 0 ? 20 : 28;
    if (box.end - box.body < size || !read_bytes(input, box.body, bytes, size) ||
        big_endian(bytes + size - 4) != 0x00010000) {
        return false;
    }
    edit->duration = bytes[0] == 0 ? big_endian(bytes + 8) : big_wide(bytes + 8);
    edit->media_time = bytes[0] == 0 ? big_endian(bytes + 12) : big_wide(bytes + 16);
    uint64_t limit = bytes[0] == 0 ? INT32_MAX : INT64_MAX;
    return edit->duration != 0 && edit->media_time <= limit;
}

static void read_edits(FlacInput input, const Mp4Box *moov, const Mp4Box *trak, Mp4Track *track)
{
    Mp4Box mdia;
    EditRange edit;
    uint32_t delay;
    uint32_t valid;
    if (!read_edit(input, trak, &edit) || !find_box(input, trak, 0, MDIA, &mdia) ||
        !sample_units(edit.media_time, time_scale(input, &mdia, MDHD), track->sample_rate,
                      &delay) ||
        !sample_units(edit.duration, time_scale(input, moov, MVHD), track->sample_rate, &valid) ||
        delay > track->total_samples) {
        return;
    }
    track->delay = delay;
    uint32_t available = track->total_samples - delay;
    track->valid_samples = valid < available ? valid : available;
}

bool mp4_probe(FlacInput input, Mp4Track *track)
{
    static const uint32_t TABLE_PATH[2] = {MINF, STBL};
    Mp4Box moov = {.body = 0, .end = input.size};
    Mp4Box mdia;
    Mp4Box trak;
    Mp4Box stbl;
    Mp4Tables tables;
    *track = (Mp4Track){0};
    if (!find_path(input, &moov, &MOOV_PATH, 1) || !find_sound(input, &moov, &mdia, &trak)) {
        return false;
    }
    stbl = mdia;
    if (!find_path(input, &stbl, TABLE_PATH, 2) || !collect_tables(input, &stbl, &tables) ||
        !read_codec(input, &tables.box[0], track) || !read_tables(input, &tables, track) ||
        !read_timing(input, &tables.box[1], track, time_scale(input, &mdia, MDHD))) {
        return false;
    }
    track->valid_samples = track->total_samples;
    read_edits(input, &moov, &trak, track);
    read_gapless(input, &moov, track);
    Mp4Box root = {.body = 0, .end = input.size};
    Mp4Box mdat;
    track->audio_bytes = find_box(input, &root, 0, MDAT, &mdat) ? mdat.end - mdat.body : 0;
    return true;
}

static bool chunk_offset(FlacInput input, const Mp4Track *track, uint32_t chunk, uint32_t *offset)
{
    if (chunk == 0 || chunk > track->chunk_count) {
        return false;
    }
    uint32_t shift = track->wide_offsets ? 3 : 2;
    if (chunk - 1 > ((UINT32_MAX - track->chunks_at) >> shift)) {
        return false;
    }
    uint32_t at = track->chunks_at + ((chunk - 1) << shift);
    if (!track->wide_offsets) {
        return read_word(input, at, offset);
    }
    uint32_t high;
    return at <= UINT32_MAX - 4 && read_word(input, at, &high) && high == 0 &&
           read_word(input, at + 4, offset);
}

static bool read_run(FlacInput input, const Mp4Track *track, uint32_t run, uint32_t *first,
                     uint32_t *samples)
{
    if (run > (UINT32_MAX - track->runs_at) / 12) {
        return false;
    }
    uint32_t at = track->runs_at + 12 * run;
    uint32_t description;
    return at <= UINT32_MAX - 8 && read_word(input, at, first) &&
           read_word(input, at + 4, samples) && read_word(input, at + 8, &description) &&
           description == 1;
}

static bool run_end(FlacInput input, const Mp4Track *track, uint32_t run, uint32_t *end)
{
    if (run + 1 >= track->run_count) {
        *end = track->chunk_count + 1;
        return true;
    }
    uint32_t samples;
    return read_run(input, track, run + 1, end, &samples);
}

static bool size_at(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t index,
                    uint32_t *size)
{
    if (track->fixed_size != 0) {
        *size = track->fixed_size;
        return true;
    }
    if (index >= track->sample_count) {
        return false;
    }
    if (index < cursor->window_first || index - cursor->window_first >= cursor->window_count) {
        uint32_t left = track->sample_count - index;
        uint32_t count = left < MP4_SIZE_WINDOW ? left : MP4_SIZE_WINDOW;
        uint8_t bytes[4 * MP4_SIZE_WINDOW];
        if (index > (UINT32_MAX - track->sizes_at) / 4 ||
            !read_bytes(input, track->sizes_at + 4 * index, bytes, 4 * count)) {
            return false;
        }
        for (uint32_t item = 0; item < count; item++) {
            cursor->window[item] = big_endian(bytes + 4 * item);
        }
        cursor->window_first = index;
        cursor->window_count = count;
    }
    *size = cursor->window[index - cursor->window_first];
    return true;
}

static bool find_run(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t sample)
{
    uint32_t passed = 0;
    for (uint32_t run = 0; run < track->run_count; run++) {
        uint32_t first;
        uint32_t samples;
        uint32_t end;
        if (!read_run(input, track, run, &first, &samples) || !run_end(input, track, run, &end) ||
            (run == 0 && first != 1) || samples == 0 || end <= first) {
            return false;
        }
        uint64_t span = (uint64_t)(end - first) * samples;
        if (sample - passed < span) {
            uint32_t into = sample - passed;
            *cursor = (Mp4Cursor){.sample = sample,
                                  .chunk = first + into / samples,
                                  .in_chunk = into % samples,
                                  .chunk_samples = samples,
                                  .run = run,
                                  .run_end = end};
            return true;
        }
        passed += (uint32_t)span;
    }
    return false;
}

bool mp4_cursor_seek(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t sample)
{
    if (sample >= track->sample_count || !find_run(cursor, input, track, sample) ||
        !chunk_offset(input, track, cursor->chunk, &cursor->offset)) {
        return false;
    }
    for (uint32_t index = sample - cursor->in_chunk; index < sample; index++) {
        uint32_t size;
        if (!size_at(cursor, input, track, index, &size) || size > UINT32_MAX - cursor->offset) {
            return false;
        }
        cursor->offset += size;
    }
    return true;
}

static bool next_chunk(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track)
{
    cursor->chunk++;
    cursor->in_chunk = 0;
    if (cursor->chunk >= cursor->run_end && cursor->run + 1 < track->run_count) {
        uint32_t first;
        cursor->run++;
        if (!read_run(input, track, cursor->run, &first, &cursor->chunk_samples) ||
            !run_end(input, track, cursor->run, &cursor->run_end) || cursor->chunk_samples == 0) {
            return false;
        }
    }
    return chunk_offset(input, track, cursor->chunk, &cursor->offset);
}

bool mp4_cursor_next(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track, uint32_t *offset,
                     uint32_t *size)
{
    if (cursor->failed || cursor->sample >= track->sample_count) {
        return false;
    }
    if (!size_at(cursor, input, track, cursor->sample, size) ||
        *size > UINT32_MAX - cursor->offset) {
        cursor->failed = true;
        return false;
    }
    *offset = cursor->offset;
    cursor->sample++;
    cursor->in_chunk++;
    cursor->offset += *size;
    if (cursor->in_chunk >= cursor->chunk_samples && cursor->sample < track->sample_count &&
        !next_chunk(cursor, input, track)) {
        cursor->failed = true;
    }
    return true;
}
