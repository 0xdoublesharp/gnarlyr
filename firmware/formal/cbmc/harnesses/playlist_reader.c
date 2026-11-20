#include "library/playlist_reader.h"
#include "formal.h"

static PlaylistReader reader;

void harness(void)
{
    char entry[FORMAL_TEXT_BYTES];
    formal_card_fill();
    if (!playlist_reader_open(&reader, formal_card_source(), formal_card_size)) {
        return;
    }
    for (uint32_t line = 0; line < FORMAL_PLAYLIST_ENTRIES; line++) {
        uint32_t capacity = 1 + formal_below(FORMAL_TEXT_BYTES);
        if (!playlist_reader_next(&reader, entry, capacity)) {
            break;
        }
        formal_check_text(entry, capacity);
    }
}
