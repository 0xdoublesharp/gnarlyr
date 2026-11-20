#include "library/cue_sheet.h"
#include "formal.h"

static CueSheet sheet;

static void check_entry(const CueEntry *entry)
{
    __CPROVER_assert(entry->title[CUE_TEXT_CAPACITY - 1] == '\0',
                     "a track title ends in its field");
    __CPROVER_assert(entry->performer[CUE_TEXT_CAPACITY - 1] == '\0',
                     "a track performer ends in its field");
}

void harness(void)
{
    formal_card_fill();
    cue_sheet_open(&sheet, formal_card_source(), formal_card_size);
    for (uint32_t entry = 0; entry < FORMAL_CUE_ENTRIES; entry++) {
        const CueEntry *next = cue_sheet_next(&sheet);
        if (next == NULL) {
            break;
        }
        check_entry(next);
    }
    __CPROVER_assert(sheet.album.file[CUE_TEXT_CAPACITY - 1] == '\0',
                     "the file name ends in its field");
    __CPROVER_assert(sheet.album.title[CUE_TEXT_CAPACITY - 1] == '\0',
                     "the album ends in its field");
}
