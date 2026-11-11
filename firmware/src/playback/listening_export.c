#include "playback/listening_export.h"
#include "core/seam.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "core/utf8.h"

enum {
    STAGE_MEASURE,
    STAGE_WRITE,
    STAGE_PAD
};

enum {
    NUMBER_BYTES = 11,
    SECOND_MS = 1000,
    FIRST_PRINTABLE = 32,
    DELETE = 127
};

const char LISTENING_EXPORT_PATH[] = "/.scrobbler.log";

static const char HEADER[] =
    "#AUDIOSCROBBLER/1.1\n#TZ/UNKNOWN\n#CLIENT/" FIRMWARE_NAME " " FIRMWARE_VERSION "\n";

typedef struct Sink {
    ListeningExport *job;
    uint32_t bytes;
    bool store;
} Sink;

static void flush(ListeningExport *job)
{
    if (!store_file_write(&job->file, job->offset, job->block, job->fill)) {
        job->result = STORE_FAILED;
    }
    job->offset += job->fill;
    job->fill = 0;
}

static void put(Sink *sink, char byte)
{
    ListeningExport *job = sink->job;
    sink->bytes++;
    if (!sink->store) {
        return;
    }
    job->block[job->fill++] = (uint8_t)byte;
    if (job->fill == STORE_BLOCK_BYTES) {
        flush(job);
    }
}

static void put_field(Sink *sink, const char *text, size_t capacity, char end)
{
    size_t length = 0;
    while (length + 1 < capacity && text[length] != '\0') {
        length++;
    }
    length = utf8_complete_length(text, length);
    for (size_t index = 0; index < length; index++) {
        uint8_t byte = (uint8_t)text[index];
        put(sink, byte < FIRST_PRINTABLE || byte == DELETE ? ' ' : text[index]);
    }
    put(sink, end);
}

static void put_number(Sink *sink, uint32_t value)
{
    char digits[NUMBER_BYTES];
    StringBuilder builder = string_builder_make(digits, sizeof digits);
    string_builder_append_unsigned(&builder, value);
    put_field(sink, digits, sizeof digits, '\t');
}

static void put_line(Sink *sink, const ListeningPlay *play, const ListeningText *text)
{
    put_field(sink, text->artist, sizeof text->artist, '\t');
    put_field(sink, text->album, sizeof text->album, '\t');
    put_field(sink, text->title, sizeof text->title, '\t');
    if (text->number != 0) {
        put_number(sink, text->number);
    } else {
        put(sink, '\t');
    }
    put_number(sink, play->length_ms / SECOND_MS);
    put(sink, listening_play_listened(play) ? 'L' : 'S');
    put(sink, '\t');
    put_number(sink, play->clock_s);
    put_field(sink, text->recording, sizeof text->recording, '\n');
}

static uint32_t room(const ListeningExport *job)
{
    return job->file.size - job->offset - job->fill;
}

static void emit(void *context, const ListeningRecord *record)
{
    ListeningExport *job = context;
    ListeningText text = {0};
    Sink sink = {.job = job};
    if (!job->lookup.text(job->lookup.context, &record->play.track, &text)) {
        job->unknown++;
        return;
    }
    put_line(&sink, &record->play, &text);
    if (job->stage == STAGE_MEASURE) {
        job->bytes += sink.bytes;
        job->plays++;
    } else if (job->excess > 0 || sink.bytes > room(job)) {
        job->excess -= sink.bytes < job->excess ? sink.bytes : job->excess;
        job->dropped++;
    } else {
        sink.store = true;
        put_line(&sink, &record->play, &text);
        job->exported++;
    }
}

static char pad_byte(const ListeningExport *job)
{
    uint32_t index = job->offset + job->fill - job->text_end;
    uint32_t column = index % LISTENING_PAD_LINE_BYTES;
    bool last = index + 1 == job->file.size - job->text_end;
    if (column == 0) {
        return '#';
    }
    return column == LISTENING_PAD_LINE_BYTES - 1 || last ? '\n' : ' ';
}

static StoreResult pad(ListeningExport *job)
{
    Sink sink = {.job = job, .store = true};
    while (room(job) > 0 && sink.bytes < STORE_BLOCK_BYTES) {
        put(&sink, pad_byte(job));
    }
    if (job->result != STORE_WORKING || room(job) > 0) {
        return job->result;
    }
    if (job->fill > 0) {
        flush(job);
    }
    return job->result == STORE_WORKING ? STORE_DONE : job->result;
}

void listening_export_begin(ListeningExport *job, const ListeningLog *log, ListeningLookup lookup)
{
    *job = (ListeningExport){.lookup = lookup,
                             .bytes = sizeof HEADER - 1,
                             .result = lookup.text != NULL ? STORE_WORKING : STORE_FAILED};
    listening_cursor_begin(&job->cursor, log, LISTENING_SECTOR_RECORDS);
}

bool listening_export_write(ListeningExport *job, StoreFile file)
{
    Sink sink = {.job = job, .store = true};
    if (job->result != STORE_DONE || job->stage != STAGE_MEASURE || file.write == NULL ||
        file.size < sizeof HEADER - 1) {
        return false;
    }
    job->file = file;
    job->excess = job->bytes > file.size ? job->bytes - file.size : 0;
    job->unknown = 0;
    job->stage = STAGE_WRITE;
    job->result = STORE_WORKING;
    listening_cursor_rewind(&job->cursor);
    for (size_t index = 0; index + 1 < sizeof HEADER; index++) {
        put(&sink, HEADER[index]);
    }
    return true;
}

CORE_SEAM StoreResult listening_export_step(ListeningExport *job)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    if (job->stage == STAGE_PAD) {
        job->result = pad(job);
        return job->result;
    }
    StoreResult result = listening_cursor_step(&job->cursor, (ListeningVisitor){emit, job});
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    if (result == STORE_DONE && job->stage == STAGE_WRITE) {
        job->stage = STAGE_PAD;
        job->text_end = job->offset + job->fill;
        result = STORE_WORKING;
    }
    job->result = result;
    return result;
}
