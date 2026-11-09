#include "library/cue_sheet.h"

#include <stddef.h>

enum {
    FRAMES_PER_SECOND = 75,
    MILLISECONDS_PER_SECOND = 1000,
    NUMBER_CEILING = 1000000
};

typedef enum CueKind {
    CUE_NONE,
    CUE_FILE,
    CUE_TRACK,
    CUE_TITLE,
    CUE_PERFORMER,
    CUE_INDEX,
    CUE_REM,
    CUE_GENRE,
    CUE_DATE
} CueKind;

typedef struct CueWord {
    const char *word;
    CueKind kind;
} CueWord;

static const CueWord KEYWORDS[] = {
    {"file", CUE_FILE},           {"track", CUE_TRACK}, {"title", CUE_TITLE},
    {"performer", CUE_PERFORMER}, {"index", CUE_INDEX}, {"rem", CUE_REM},
};

static const CueWord REM_WORDS[] = {{"genre", CUE_GENRE}, {"date", CUE_DATE}};

static bool is_blank(char character)
{
    return character == ' ' || character == '\t';
}

static char *skip_blanks(char *text)
{
    while (is_blank(*text)) {
        text++;
    }
    return text;
}

static char fold(char character)
{
    return (char)(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
}

static char *after_word(char *line, const char *word)
{
    size_t length = 0;
    while (word[length] != '\0' && fold(line[length]) == word[length]) {
        length++;
    }
    if (word[length] != '\0' || (line[length] != '\0' && !is_blank(line[length]))) {
        return NULL;
    }
    return skip_blanks(line + length);
}

static CueKind find_word(const CueWord *words, size_t count, char **rest)
{
    for (size_t index = 0; index < count; index++) {
        char *after = after_word(*rest, words[index].word);
        if (after != NULL) {
            *rest = after;
            return words[index].kind;
        }
    }
    return CUE_NONE;
}

static char *value_of(char *rest, bool whole)
{
    char *end = rest;
    if (rest[0] == '"') {
        rest++;
        end = rest;
        while (*end != '\0' && *end != '"') {
            end++;
        }
    } else if (!whole) {
        while (*end != '\0' && !is_blank(*end)) {
            end++;
        }
    } else {
        return rest;
    }
    *end = '\0';
    return rest;
}

static char *read_digits(char *text, uint32_t *value)
{
    *value = 0;
    while (*text >= '0' && *text <= '9') {
        *value = *value < NUMBER_CEILING ? *value * 10 + (uint32_t)(*text - '0') : *value;
        text++;
    }
    return text;
}

static bool read_index(char *rest, uint32_t *start_ms)
{
    uint32_t number = 0;
    uint32_t fields[3];
    char *cursor = read_digits(rest, &number);
    if (cursor == rest || !is_blank(*cursor) || number != 1) {
        return false;
    }
    cursor = skip_blanks(cursor);
    for (int field = 0; field < 3; field++) {
        const char *start = cursor;
        cursor = read_digits(cursor, &fields[field]);
        if (cursor == start || (field < 2 && *cursor++ != ':')) {
            return false;
        }
    }
    uint64_t frames = ((uint64_t)fields[0] * 60 + fields[1]) * FRAMES_PER_SECOND + fields[2];
    uint64_t milliseconds = frames * MILLISECONDS_PER_SECOND / FRAMES_PER_SECOND;
    *start_ms = milliseconds < CUE_NO_START ? (uint32_t)milliseconds : CUE_NO_START - 1;
    return true;
}

static void copy_text(char *destination, const char *source)
{
    size_t length = 0;
    while (source[length] != '\0' && length + 1 < CUE_TEXT_CAPACITY) {
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
}

static void begin_building(CueSheet *sheet, uint32_t number)
{
    sheet->building = (CueEntry){.number = number, .start_ms = CUE_NO_START};
    sheet->has_building = true;
}

static void set_text(CueSheet *sheet, CueKind kind, const char *text)
{
    CueEntry *track = &sheet->building;
    switch (kind) {
    case CUE_TITLE:
        copy_text(sheet->has_building ? track->title : sheet->album.title, text);
        return;
    case CUE_PERFORMER:
        copy_text(sheet->has_building ? track->performer : sheet->album.performer, text);
        return;
    case CUE_GENRE:
        copy_text(sheet->album.genre, text);
        return;
    case CUE_DATE:
        copy_text(sheet->album.date, text);
        return;
    default:
        return;
    }
}

static void note_file(CueSheet *sheet, char *rest)
{
    if (sheet->files++ == 0) {
        copy_text(sheet->album.file, value_of(rest, false));
    }
}

static const CueEntry *close_building(CueSheet *sheet)
{
    const CueEntry *ready = NULL;
    if (sheet->has_building && sheet->building.start_ms != CUE_NO_START) {
        if (sheet->has_pending) {
            sheet->out = sheet->pending;
            sheet->out.end_ms = sheet->building.start_ms;
            ready = &sheet->out;
        }
        sheet->pending = sheet->building;
        sheet->has_pending = true;
    }
    sheet->has_building = false;
    return ready;
}

static const CueEntry *finish(CueSheet *sheet)
{
    const CueEntry *ready = close_building(sheet);
    if (ready == NULL && sheet->has_pending) {
        sheet->out = sheet->pending;
        sheet->out.end_ms = 0;
        sheet->has_pending = false;
        ready = &sheet->out;
    }
    return ready;
}

static void note_rem(CueSheet *sheet, char *rest)
{
    CueKind kind = find_word(REM_WORDS, sizeof REM_WORDS / sizeof REM_WORDS[0], &rest);
    if (kind != CUE_NONE && !sheet->has_building) {
        set_text(sheet, kind, value_of(rest, true));
    }
}

static const CueEntry *handle_line(CueSheet *sheet, char *line)
{
    char *rest = line;
    CueKind kind = find_word(KEYWORDS, sizeof KEYWORDS / sizeof KEYWORDS[0], &rest);
    uint32_t value = 0;
    const CueEntry *ready = NULL;
    if (kind == CUE_FILE) {
        note_file(sheet, rest);
    } else if (kind == CUE_TRACK) {
        ready = close_building(sheet);
        read_digits(rest, &value);
        begin_building(sheet, value);
    } else if (kind == CUE_TITLE || kind == CUE_PERFORMER) {
        set_text(sheet, kind, value_of(rest, true));
    } else if (kind == CUE_INDEX && sheet->has_building && read_index(rest, &value)) {
        sheet->building.start_ms = value;
    } else if (kind == CUE_REM) {
        note_rem(sheet, rest);
    }
    return ready;
}

void cue_sheet_open(CueSheet *sheet, ByteSource source, uint32_t size)
{
    sheet->album.file[0] = '\0';
    sheet->album.title[0] = '\0';
    sheet->album.performer[0] = '\0';
    sheet->album.genre[0] = '\0';
    sheet->album.date[0] = '\0';
    sheet->has_building = false;
    sheet->has_pending = false;
    sheet->files = 0;
    playlist_reader_open(&sheet->reader, source, size);
}

const CueEntry *cue_sheet_next(CueSheet *sheet)
{
    while (playlist_reader_next(&sheet->reader, sheet->line, sizeof sheet->line)) {
        const CueEntry *ready = handle_line(sheet, sheet->line);
        if (ready != NULL) {
            return ready;
        }
    }
    return finish(sheet);
}

bool cue_sheet_usable(CueSheet *sheet)
{
    uint32_t tracks = 0;
    while (cue_sheet_next(sheet) != NULL) {
        tracks++;
    }
    return sheet->files == 1 && tracks > 0;
}
