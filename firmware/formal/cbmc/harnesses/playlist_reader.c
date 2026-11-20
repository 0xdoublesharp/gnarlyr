#include "formal.h"
#include "library/playlist_reader.h"

static PlaylistReader reader;

static void resume_anywhere(void)
{
    reader.offset = formal_below(formal_card_size + 1);
    reader.chunk_start = formal_below(reader.offset + 1);
    uint32_t left = formal_card_size - reader.chunk_start;
    reader.chunk_length = formal_below((left < PLAYLIST_CHUNK ? left : PLAYLIST_CHUNK) + 1);
    __CPROVER_havoc_object(reader.chunk);
    reader.utf8 = nondet_bool();
    reader.start_ms = nondet_uint32();
}

void harness(void)
{
    char entry[FORMAL_TEXT_BYTES];
    formal_card_fill();
    (void)playlist_reader_open(&reader, formal_card_source(), formal_card_size);
    if (nondet_bool()) {
        resume_anywhere();
    }
    uint32_t capacity = 1 + formal_below(FORMAL_TEXT_BYTES);
    if (playlist_reader_next(&reader, entry, capacity)) {
        formal_check_text(entry, capacity);
    }
}
