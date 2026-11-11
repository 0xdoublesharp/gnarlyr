#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuzz_source.h"
#include "library/cue_sheet.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_text(const char *text)
{
    fuzz_require(memchr(text, '\0', CUE_TEXT_CAPACITY) != NULL);
}

static bool same_entry(const CueEntry *first, const CueEntry *second)
{
    return first->number == second->number && first->start_ms == second->start_ms &&
           first->end_ms == second->end_ms && strcmp(first->title, second->title) == 0 &&
           strcmp(first->performer, second->performer) == 0;
}

static bool same_album(const CueAlbum *first, const CueAlbum *second)
{
    return strcmp(first->file, second->file) == 0 && strcmp(first->title, second->title) == 0 &&
           strcmp(first->performer, second->performer) == 0 &&
           strcmp(first->genre, second->genre) == 0 && strcmp(first->date, second->date) == 0;
}

static void check_entry(const CueEntry *entry)
{
    fuzz_require(entry->number >= 1 && entry->number <= 99);
    fuzz_require(entry->start_ms != CUE_NO_START);
    fuzz_require(entry->end_ms == 0 || entry->end_ms > entry->start_ms);
    check_text(entry->title);
    check_text(entry->performer);
}

static void check_album(const CueAlbum *album)
{
    check_text(album->file);
    check_text(album->title);
    check_text(album->performer);
    check_text(album->genre);
    check_text(album->date);
}

static void check_next(const CueEntry *first, const CueEntry *second, const CueEntry *previous,
                       bool has_previous)
{
    fuzz_require((first == NULL) == (second == NULL));
    if (first == NULL) {
        return;
    }
    check_entry(first);
    check_entry(second);
    fuzz_require(same_entry(first, second));
    if (has_previous) {
        fuzz_require(previous->end_ms == first->start_ms);
        fuzz_require(previous->start_ms < first->start_ms);
    }
}

static uint32_t check_entries(CueSheet *first, CueSheet *second, uint32_t size)
{
    CueEntry previous = {0};
    bool has_previous = false;
    uint32_t call = 0;
    while (true) {
        const CueEntry *first_entry = cue_sheet_next(first);
        const CueEntry *second_entry = cue_sheet_next(second);
        check_next(first_entry, second_entry, &previous, has_previous);
        if (first_entry == NULL) {
            fuzz_require(!has_previous || previous.end_ms == 0);
            return call;
        }
        previous = *first_entry;
        has_previous = true;
        fuzz_require(call < size);
        call++;
    }
}

static void check_usable(const uint8_t *data, uint32_t size, uint32_t count, uint32_t files)
{
    FuzzSource source = {.bytes = data, .size = size};
    CueSheet sheet;
    cue_sheet_open(&sheet, fuzz_source(&source), size);
    fuzz_require(cue_sheet_usable(&sheet) == (files == 1 && count > 0));
    fuzz_require(!source.invalid_read);
}

static void check_sheet(const uint8_t *data, uint32_t size)
{
    FuzzSource first_source = {.bytes = data, .size = size};
    FuzzSource second_source = first_source;
    CueSheet first;
    CueSheet second;
    cue_sheet_open(&first, fuzz_source(&first_source), size);
    cue_sheet_open(&second, fuzz_source(&second_source), size);
    uint32_t count = check_entries(&first, &second, size);
    check_album(&first.album);
    check_album(&second.album);
    fuzz_require(same_album(&first.album, &second.album));
    fuzz_require(first.files == second.files);
    fuzz_require(!first_source.invalid_read && !second_source.invalid_read);
    check_usable(data, size, count, first.files);
}

static void check_invalid_index(const uint8_t *data, size_t size)
{
    if (size == 0) {
        return;
    }
    uint32_t selected = data[0] % 3u;
    uint32_t seconds = selected == 0 ? 60u + (size > 1 ? data[1] : 0u) : data[0] % 60u;
    uint32_t frames = selected == 1 ? 75u + (size > 1 ? data[1] : 0u) : data[0] % 75u;
    const char *suffix = selected == 2 ? " trailing" : "";
    char text[128];
    int length =
        snprintf(text, sizeof text, "FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:%02u:%02u%s\n",
                 seconds, frames, suffix);
    fuzz_require(length > 0 && (size_t)length < sizeof text);
    FuzzSource source = {.bytes = (const uint8_t *)text, .size = (uint32_t)length};
    CueSheet sheet;
    cue_sheet_open(&sheet, fuzz_source(&source), (uint32_t)length);
    fuzz_require(cue_sheet_next(&sheet) == NULL);
    fuzz_require(!source.invalid_read);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size <= UINT32_MAX) {
        check_sheet(data, (uint32_t)size);
        check_invalid_index(data, size);
    }
    return 0;
}
