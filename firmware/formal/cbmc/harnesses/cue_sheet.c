#include "formal.h"
#include "library/cue_sheet.h"

static CueSheet sheet;

static void resume_anywhere(void)
{
    PlaylistReader *reader = &sheet.reader;
    reader->offset = formal_below(formal_card_size + 1);
    reader->chunk_start = reader->offset;
    reader->chunk_length = 0;
    reader->utf8 = nondet_bool();
    sheet.has_building = nondet_bool();
    sheet.has_pending = nondet_bool();
    sheet.building.number = nondet_uint32();
    sheet.building.start_ms = nondet_uint32();
    sheet.pending.number = nondet_uint32();
    sheet.pending.start_ms = nondet_uint32();
    sheet.files = formal_below(3);
}

static void check_entry(const CueEntry *entry)
{
    __CPROVER_assert(entry->title[CUE_TEXT_CAPACITY - 1] == '\0', "a track title ends in its field");
    __CPROVER_assert(entry->performer[CUE_TEXT_CAPACITY - 1] == '\0',
                     "a track performer ends in its field");
}

void harness(void)
{
    formal_card_fill();
    cue_sheet_open(&sheet, formal_card_source(), formal_card_size);
    if (nondet_bool()) {
        resume_anywhere();
    }
    const CueEntry *next = cue_sheet_next(&sheet);
    if (next != NULL) {
        check_entry(next);
    }
    __CPROVER_assert(sheet.album.file[CUE_TEXT_CAPACITY - 1] == '\0', "the file name ends in its field");
    __CPROVER_assert(sheet.album.title[CUE_TEXT_CAPACITY - 1] == '\0', "the album ends in its field");
}
