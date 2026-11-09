#include "mp4_chapters.h"

#include <stddef.h>

enum {
    FULL_BOX = 4,
    CHPL_RESERVED = 4,
    CHPL_ENTRY = 9,
    CHPL_UNITS_PER_MS = 10000,
    TEXT_PREFIX = 2,
    SAMPLE_LIMIT = 1024,
    TABLE_HEAD = 8,
    STTS_ENTRY = 8,
    STSC_ENTRY = 12,
    STSZ_HEAD = 12,
    MIN_CHAPTERS = 2,
    MAX_LENGTH = 0xFFFF
};

typedef struct Candidate {
    uint32_t start_ms;
    uint32_t offset;
    uint32_t length;
} Candidate;

typedef struct TextTables {
    Mp4Atom stts;
    Mp4Atom stsz;
    Mp4Atom stsc;
    Mp4Atom chunks;
    bool wide;
    uint32_t timescale;
} TextTables;

typedef struct Walk {
    uint32_t sample;
    uint32_t sample_count;
    uint32_t fixed_size;
    uint32_t time_runs;
    uint32_t time_run;
    uint32_t time_left;
    uint32_t time_delta;
    uint64_t time;
    uint32_t chunk_count;
    uint32_t chunk;
    uint32_t chunk_runs;
    uint32_t chunk_run;
    uint32_t per_chunk;
    uint32_t in_chunk;
    uint32_t cursor;
} Walk;

static bool read_u32(const TagReader *reader, uint32_t offset, uint32_t *value)
{
    uint8_t bytes[4];
    if (!tag_read(reader, offset, bytes, sizeof bytes)) {
        return false;
    }
    *value = tag_be32(bytes);
    return true;
}

static void add_chapter(Mp4Chapters *chapters, uint32_t duration_ms, Candidate candidate)
{
    uint32_t count = chapters->count;
    bool late = duration_ms != 0 && candidate.start_ms >= duration_ms;
    bool repeated = count > 0 && candidate.start_ms <= chapters->start_ms[count - 1];
    if (count >= MP4_CHAPTER_MAX || late || repeated) {
        return;
    }
    chapters->start_ms[count] = count == 0 ? 0 : candidate.start_ms;
    chapters->offset[count] = candidate.offset;
    chapters->length[count] =
        (uint16_t)(candidate.length < MAX_LENGTH ? candidate.length : MAX_LENGTH);
    chapters->count++;
}

static bool read_entries(Mp4Chapters *chapters, const Mp4Atom *chpl, uint32_t at,
                         uint32_t duration_ms)
{
    uint8_t count = 0;
    if (!tag_read(&chapters->reader, at, &count, 1)) {
        return false;
    }
    uint32_t cursor = at + 1;
    for (uint32_t index = 0; index < count; index++) {
        uint8_t entry[CHPL_ENTRY];
        if (chpl->end - cursor < CHPL_ENTRY ||
            !tag_read(&chapters->reader, cursor, entry, CHPL_ENTRY)) {
            return false;
        }
        uint64_t start = tag_be64(entry) / CHPL_UNITS_PER_MS;
        uint32_t length = entry[8];
        if (start > UINT32_MAX || length > chpl->end - cursor - CHPL_ENTRY) {
            return false;
        }
        add_chapter(chapters, duration_ms,
                    (Candidate){(uint32_t)start, cursor + CHPL_ENTRY, length});
        cursor += CHPL_ENTRY + length;
    }
    return true;
}

static bool read_chpl(Mp4Chapters *chapters, const Mp4Atom *moov, uint32_t duration_ms)
{
    static const uint32_t SKIPS[] = {FULL_BOX + CHPL_RESERVED, FULL_BOX};
    Mp4Atom udta;
    Mp4Atom chpl;
    if (!tags_mp4_find_child(&chapters->reader, moov, 0, "udta", &udta) ||
        !tags_mp4_find_child(&chapters->reader, &udta, 0, "chpl", &chpl)) {
        return false;
    }
    for (size_t index = 0; index < sizeof SKIPS / sizeof SKIPS[0]; index++) {
        chapters->count = 0;
        if (chpl.end - chpl.body > SKIPS[index] &&
            read_entries(chapters, &chpl, chpl.body + SKIPS[index], duration_ms) &&
            chapters->count >= MIN_CHAPTERS) {
            chapters->prefixed = false;
            return true;
        }
    }
    return false;
}

static bool chapter_track_id(const TagReader *reader, const Mp4Atom *moov, uint32_t *id)
{
    uint32_t cursor = moov->body;
    Mp4Atom trak;
    Mp4Atom tref;
    Mp4Atom chap;
    while (tags_mp4_read_atom(reader, cursor, moov->end, &trak)) {
        if (tag_same_text(trak.type, 4, "trak") &&
            tags_mp4_find_child(reader, &trak, 0, "tref", &tref) &&
            tags_mp4_find_child(reader, &tref, 0, "chap", &chap) &&
            chap.end - chap.body >= sizeof(uint32_t)) {
            return read_u32(reader, chap.body, id);
        }
        cursor = trak.end;
    }
    return false;
}

static bool has_track_id(const TagReader *reader, const Mp4Atom *trak, uint32_t id)
{
    Mp4Atom tkhd;
    uint8_t version = 0;
    uint32_t found = 0;
    if (!tags_mp4_find_child(reader, trak, 0, "tkhd", &tkhd) ||
        !tag_read(reader, tkhd.body, &version, 1)) {
        return false;
    }
    uint32_t at = tkhd.body + (version == 1 ? 20 : 12);
    return at + sizeof found <= tkhd.end && read_u32(reader, at, &found) && found == id;
}

static bool find_track(const TagReader *reader, const Mp4Atom *moov, uint32_t id, Mp4Atom *trak)
{
    uint32_t cursor = moov->body;
    while (tags_mp4_read_atom(reader, cursor, moov->end, trak)) {
        if (tag_same_text(trak->type, 4, "trak") && has_track_id(reader, trak, id)) {
            return true;
        }
        cursor = trak->end;
    }
    return false;
}

static bool read_timescale(const TagReader *reader, const Mp4Atom *mdia, uint32_t *timescale)
{
    Mp4Atom mdhd;
    uint8_t version = 0;
    if (!tags_mp4_find_child(reader, mdia, 0, "mdhd", &mdhd) ||
        !tag_read(reader, mdhd.body, &version, 1)) {
        return false;
    }
    uint32_t at = mdhd.body + (version == 1 ? 20 : 12);
    return at + sizeof *timescale <= mdhd.end && read_u32(reader, at, timescale) && *timescale > 0;
}

static bool find_chunks(const TagReader *reader, const Mp4Atom *stbl, TextTables *tables)
{
    tables->wide = false;
    if (tags_mp4_find_child(reader, stbl, 0, "stco", &tables->chunks)) {
        return true;
    }
    tables->wide = true;
    return tags_mp4_find_child(reader, stbl, 0, "co64", &tables->chunks);
}

static bool locate_tables(const TagReader *reader, const Mp4Atom *trak, TextTables *tables)
{
    Mp4Atom mdia;
    Mp4Atom minf;
    Mp4Atom stbl;
    return tags_mp4_find_child(reader, trak, 0, "mdia", &mdia) &&
           read_timescale(reader, &mdia, &tables->timescale) &&
           tags_mp4_find_child(reader, &mdia, 0, "minf", &minf) &&
           tags_mp4_find_child(reader, &minf, 0, "stbl", &stbl) &&
           tags_mp4_find_child(reader, &stbl, 0, "stts", &tables->stts) &&
           tags_mp4_find_child(reader, &stbl, 0, "stsz", &tables->stsz) &&
           tags_mp4_find_child(reader, &stbl, 0, "stsc", &tables->stsc) &&
           find_chunks(reader, &stbl, tables);
}

static bool table_fits(const Mp4Atom *atom, uint32_t entries, uint32_t width)
{
    return atom->end - atom->body >= TABLE_HEAD &&
           (uint64_t)entries * width <= atom->end - atom->body - TABLE_HEAD;
}

static bool open_walk(const TagReader *reader, const TextTables *tables, Walk *walk)
{
    *walk = (Walk){0};
    uint32_t chunk_width = tables->wide ? 8 : 4;
    bool sized = tables->stsz.end - tables->stsz.body >= STSZ_HEAD &&
                 read_u32(reader, tables->stsz.body + 4, &walk->fixed_size) &&
                 read_u32(reader, tables->stsz.body + 8, &walk->sample_count);
    bool counted = read_u32(reader, tables->stts.body + 4, &walk->time_runs) &&
                   read_u32(reader, tables->stsc.body + 4, &walk->chunk_runs) &&
                   read_u32(reader, tables->chunks.body + 4, &walk->chunk_count);
    return sized && counted && table_fits(&tables->stts, walk->time_runs, STTS_ENTRY) &&
           table_fits(&tables->stsc, walk->chunk_runs, STSC_ENTRY) &&
           table_fits(&tables->chunks, walk->chunk_count, chunk_width) &&
           (walk->fixed_size != 0 || table_fits(&tables->stsz, walk->sample_count, 4) ||
            walk->sample_count == 0);
}

static bool walk_time(const TagReader *reader, const TextTables *tables, Walk *walk, uint64_t *time)
{
    while (walk->time_left == 0) {
        if (walk->time_run >= walk->time_runs) {
            return false;
        }
        uint32_t at = tables->stts.body + TABLE_HEAD + STTS_ENTRY * walk->time_run++;
        if (!read_u32(reader, at, &walk->time_left) ||
            !read_u32(reader, at + 4, &walk->time_delta)) {
            return false;
        }
    }
    *time = walk->time;
    walk->time += walk->time_delta;
    walk->time_left--;
    return true;
}

static bool chunk_offset(const TagReader *reader, const TextTables *tables, uint32_t chunk,
                         uint32_t *offset)
{
    uint8_t bytes[8];
    uint32_t width = tables->wide ? 8 : 4;
    if (!tag_read(reader, tables->chunks.body + TABLE_HEAD + width * (chunk - 1), bytes, width)) {
        return false;
    }
    *offset = tag_be32(bytes + width - 4);
    return !tables->wide || tag_be32(bytes) == 0;
}

static bool enter_chunk(const TagReader *reader, const TextTables *tables, Walk *walk)
{
    if (walk->chunk >= walk->chunk_count) {
        return false;
    }
    walk->chunk++;
    uint32_t first = 0;
    while (walk->chunk_run + 1 < walk->chunk_runs &&
           read_u32(reader, tables->stsc.body + TABLE_HEAD + STSC_ENTRY * (walk->chunk_run + 1),
                    &first) &&
           first <= walk->chunk) {
        walk->chunk_run++;
    }
    walk->in_chunk = 0;
    return read_u32(reader, tables->stsc.body + TABLE_HEAD + STSC_ENTRY * walk->chunk_run + 4,
                    &walk->per_chunk) &&
           walk->per_chunk > 0 && chunk_offset(reader, tables, walk->chunk, &walk->cursor);
}

static bool sample_size(const TagReader *reader, const TextTables *tables, const Walk *walk,
                        uint32_t *size)
{
    if (walk->fixed_size != 0) {
        *size = walk->fixed_size;
        return true;
    }
    return read_u32(reader, tables->stsz.body + STSZ_HEAD + 4 * walk->sample, size);
}

static bool walk_place(const TagReader *reader, const TextTables *tables, Walk *walk,
                       Candidate *candidate)
{
    uint32_t size = 0;
    if ((walk->in_chunk >= walk->per_chunk && !enter_chunk(reader, tables, walk)) ||
        !sample_size(reader, tables, walk, &size) || walk->cursor > reader->size ||
        size > reader->size - walk->cursor) {
        return false;
    }
    candidate->offset = walk->cursor;
    candidate->length = size;
    walk->cursor += size;
    walk->in_chunk++;
    walk->sample++;
    return true;
}

static uint32_t milliseconds(uint64_t time, uint32_t timescale)
{
    uint64_t value = time * 1000u / timescale;
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static bool read_samples(Mp4Chapters *chapters, const TextTables *tables, uint32_t duration_ms)
{
    Walk walk;
    if (!open_walk(&chapters->reader, tables, &walk)) {
        return false;
    }
    while (walk.sample < walk.sample_count && walk.sample < SAMPLE_LIMIT &&
           chapters->count < MP4_CHAPTER_MAX) {
        uint64_t time = 0;
        Candidate candidate = {0};
        if (!walk_time(&chapters->reader, tables, &walk, &time) ||
            !walk_place(&chapters->reader, tables, &walk, &candidate)) {
            return false;
        }
        candidate.start_ms = milliseconds(time, tables->timescale);
        add_chapter(chapters, duration_ms, candidate);
    }
    return true;
}

static bool read_text_track(Mp4Chapters *chapters, const Mp4Atom *moov, uint32_t duration_ms)
{
    uint32_t id = 0;
    Mp4Atom trak;
    TextTables tables;
    chapters->count = 0;
    chapters->prefixed = true;
    return chapter_track_id(&chapters->reader, moov, &id) &&
           find_track(&chapters->reader, moov, id, &trak) &&
           locate_tables(&chapters->reader, &trak, &tables) &&
           read_samples(chapters, &tables, duration_ms) && chapters->count >= MIN_CHAPTERS;
}

bool mp4_chapters_open(Mp4Chapters *chapters, ByteSource source, uint32_t size,
                       uint32_t duration_ms)
{
    Mp4Atom root = {.offset = 0, .body = 0, .end = size};
    Mp4Atom moov;
    chapters->reader = (TagReader){.source = source, .size = size, .tags = NULL};
    chapters->count = 0;
    bool found =
        tags_mp4_find_child(&chapters->reader, &root, 0, "moov", &moov) &&
        (read_chpl(chapters, &moov, duration_ms) || read_text_track(chapters, &moov, duration_ms));
    chapters->count = found ? chapters->count : 0;
    return found;
}

static uint32_t text_length(const Mp4Chapters *chapters, uint32_t index, const uint8_t *raw,
                            uint32_t read)
{
    uint32_t length = read;
    if (chapters->prefixed) {
        uint32_t declared = read >= TEXT_PREFIX ? tag_be16(raw) : 0;
        uint32_t room =
            chapters->length[index] >= TEXT_PREFIX ? chapters->length[index] - TEXT_PREFIX : 0;
        length = declared < room ? declared : room;
    }
    return length;
}

void mp4_chapter_title(const Mp4Chapters *chapters, uint32_t index, char *title)
{
    uint8_t raw[TEXT_PREFIX + MP4_CHAPTER_TITLE];
    uint32_t read = chapters->length[index] < sizeof raw ? chapters->length[index] : sizeof raw;
    uint32_t skip = chapters->prefixed ? TEXT_PREFIX : 0;
    title[0] = '\0';
    if (read < skip || !tag_read(&chapters->reader, chapters->offset[index], raw, read)) {
        return;
    }
    uint32_t length = text_length(chapters, index, raw, read);
    const uint8_t *text = raw + skip;
    uint32_t room = read - skip;
    length = length < room ? length : room;
    bool utf16 = length >= 2 &&
                 ((text[0] == 0xFE && text[1] == 0xFF) || (text[0] == 0xFF && text[1] == 0xFE));
    uint32_t used = 0;
    while (!utf16 && used < length && used < MP4_CHAPTER_TITLE - 1 && text[used] != '\0') {
        title[used] = (char)text[used];
        used++;
    }
    while (used > 0 && used < length && (text[used] & 0xC0) == 0x80) {
        used--;
    }
    title[used] = '\0';
}
