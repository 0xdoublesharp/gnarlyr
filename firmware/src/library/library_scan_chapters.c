#include <stddef.h>
#include <stdint.h>

#include "core/string_builder.h"
#include "scan_internal.h"

enum {
    CHAPTER_FALLBACK = 32,
    SMALL_MAX = 255
};

bool scan_is_chaptered_name(const char *name)
{
    return scan_has_suffix(name, ".m4b");
}

static void fill_sheet_name(ScanState *state)
{
    const char *name = state->path;
    for (const char *cursor = state->path; *cursor != '\0'; cursor++) {
        name = *cursor == '/' ? cursor + 1 : name;
    }
    uint32_t length = scan_text_length(name);
    uint32_t stem = length;
    for (uint32_t index = 0; index < length; index++) {
        stem = name[index] == '.' ? index : stem;
    }
    for (uint32_t index = 0; index < stem; index++) {
        state->sheet_name[index] = name[index];
    }
    state->sheet_name[stem] = '\0';
}

static void chapter_title(const Mp4Chapters *chapters, uint32_t index, char *title)
{
    mp4_chapter_title(chapters, index, title);
    if (title[0] == '\0') {
        StringBuilder builder = string_builder_make(title, CHAPTER_FALLBACK);
        string_builder_append(&builder, "Chapter ");
        string_builder_append_unsigned(&builder, index + 1);
    }
}

static void apply_chapter(const ScanState *state, uint32_t index, char *title, BuilderTrack *track)
{
    const Mp4Chapters *chapters = &state->chapters;
    uint32_t start = chapters->start_ms[index];
    uint32_t end = index + 1 < chapters->count ? chapters->start_ms[index + 1] : 0;
    uint32_t limit = end != 0 ? end : track->duration_ms;
    chapter_title(chapters, index, title);
    track->title = title;
    track->number = (uint8_t)(index + 1 < SMALL_MAX ? index + 1 : SMALL_MAX);
    track->start_ms = start;
    track->end_ms = end;
    track->duration_ms = limit > start ? limit - start : 0;
    track->sheet = state->sheet_name;
    track->chapters = true;
}

ScanResult scan_add_chapters(ScanState *state, const ScanJob *job, ScanStats *stats)
{
    ScanResult result = SCAN_OK;
    fill_sheet_name(state);
    for (uint32_t index = 0; result == SCAN_OK && index < state->chapters.count; index++) {
        char title[MP4_CHAPTER_TITLE];
        BuilderTrack track = state->fields.track;
        apply_chapter(state, index, title, &track);
        result = scan_add_track(state, &track);
        stats->tracks += result == SCAN_OK;
        scan_report(job, stats);
    }
    return result;
}
