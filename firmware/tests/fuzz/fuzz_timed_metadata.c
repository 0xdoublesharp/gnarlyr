#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "library/lyrics.h"
#include "mp4_chapters.h"

enum {
    TEXT_BYTES = 96,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t get32(const uint8_t *data, size_t size, size_t offset)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4 && offset + index < size; index++) {
        value |= (uint32_t)data[offset + index] << (index * 8);
    }
    return value;
}

static void exercise_lyrics(const uint8_t *data, uint32_t size)
{
    FuzzSource source = {.bytes = data, .size = size};
    LyricsIndex index;
    bool parsed = lyrics_parse(&index, fuzz_source(&source), size);
    fuzz_require(!source.invalid_read);
    fuzz_require(index.count <= LYRICS_LINE_CAPACITY);
    if (!parsed) {
        fuzz_require(index.count == 0);
        return;
    }
    uint32_t previous = 0;
    for (uint16_t line = 0; line < index.count; line++) {
        const LyricsLine *entry = &index.lines[line];
        fuzz_require(line == 0 || entry->time_ms >= previous);
        fuzz_require(entry->offset <= size && entry->bytes <= size - entry->offset);
        previous = entry->time_ms;
        char text[TEXT_BYTES];
        fuzz_require(lyrics_text(fuzz_source(&source), entry, text, sizeof text));
        fuzz_require(memchr(text, '\0', sizeof text) != NULL);
    }
    uint32_t position = get32(data, size, size > 4 ? size - 4 : 0);
    int current = lyrics_current(&index, position);
    fuzz_require(current >= -1 && current < index.count);
    if (current >= 0) {
        fuzz_require(index.lines[current].time_ms <= position);
        fuzz_require(current + 1 == index.count || index.lines[current + 1].time_ms > position);
    }
}

static void exercise_chapters(const uint8_t *data, uint32_t size)
{
    FuzzSource source = {.bytes = data, .size = size};
    Mp4Chapters chapters;
    uint32_t duration = get32(data, size, 0);
    if (!mp4_chapters_open(&chapters, fuzz_source(&source), size, duration)) {
        fuzz_require(chapters.count == 0);
        fuzz_require(!source.invalid_read);
        return;
    }
    fuzz_require(chapters.count > 0 && chapters.count <= MP4_CHAPTER_MAX);
    for (uint32_t index = 0; index < chapters.count; index++) {
        fuzz_require(index == 0 || chapters.start_ms[index] >= chapters.start_ms[index - 1]);
        fuzz_require(chapters.offset[index] <= size &&
                     chapters.length[index] <= size - chapters.offset[index]);
        char title[MP4_CHAPTER_TITLE];
        mp4_chapter_title(&chapters, index, title);
        fuzz_require(memchr(title, '\0', sizeof title) != NULL);
    }
    fuzz_require(!source.invalid_read);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }
    exercise_lyrics(data, (uint32_t)size);
    exercise_chapters(data, (uint32_t)size);
    return 0;
}
