#include <stddef.h>
#include <stdint.h>

#include "core/string_builder.h"
#include "scan_internal.h"

#define NO_TARGET 0xFFFFFFFFu

enum {
    CUE_TRACK_TITLE = 32,
    SMALL_MAX = 255
};

static const char *const AUDIO_SUFFIXES[] = {".flac", ".wav", ".mp3"};

static char fold(char character)
{
    return (char)(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
}

static bool same_folded(const char *left, const char *right, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (fold(left[index]) != fold(right[index])) {
            return false;
        }
    }
    return true;
}

bool scan_has_suffix(const char *name, const char *suffix)
{
    uint32_t length = scan_text_length(name);
    uint32_t suffix_length = scan_text_length(suffix);
    return length > suffix_length &&
           same_folded(name + length - suffix_length, suffix, suffix_length);
}

bool scan_is_cue_name(const char *name)
{
    return scan_has_suffix(name, ".cue");
}

static bool is_audio_name(const char *name)
{
    for (size_t index = 0; index < sizeof AUDIO_SUFFIXES / sizeof AUDIO_SUFFIXES[0]; index++) {
        if (scan_has_suffix(name, AUDIO_SUFFIXES[index])) {
            return true;
        }
    }
    return false;
}

static uint32_t stem_length(const char *name)
{
    uint32_t length = scan_text_length(name);
    uint32_t dot = length;
    for (uint32_t index = 0; index < length; index++) {
        dot = name[index] == '.' ? index : dot;
    }
    return dot;
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        base = *cursor == '/' || *cursor == '\\' ? cursor + 1 : base;
    }
    return base;
}

static bool names_match(const char *left, uint32_t left_length, const char *right,
                        uint32_t right_length)
{
    return left_length == right_length && same_folded(left, right, left_length);
}

static const uint32_t *level_offsets(const ScanState *state, const Level *level)
{
    return (const uint32_t *)(const void *)(state->listing + level->first);
}

static bool is_audio_entry(const ScanState *state, uint32_t offset)
{
    return state->listing[offset] != ENTRY_DIRECTORY &&
           is_audio_name(scan_entry_name(state, offset));
}

static uint32_t find_audio(const ScanState *state, const Level *level, const char *wanted,
                           bool by_stem)
{
    uint32_t length = by_stem ? stem_length(wanted) : scan_text_length(wanted);
    const uint32_t *offsets = level_offsets(state, level);
    for (uint32_t index = 0; index < level->count; index++) {
        const char *name = scan_entry_name(state, offsets[index]);
        uint32_t name_length = by_stem ? stem_length(name) : scan_text_length(name);
        if (is_audio_entry(state, offsets[index]) &&
            names_match(name, name_length, wanted, length)) {
            return offsets[index];
        }
    }
    return NO_TARGET;
}

static uint32_t resolve_target(const ScanState *state, const Level *level, const char *cue_name)
{
    uint32_t target = find_audio(state, level, base_name(state->cue.album.file), false);
    return target != NO_TARGET ? target : find_audio(state, level, cue_name, true);
}

static bool open_cue(ScanState *state, const ScanFileSystem *files, const char *name)
{
    ByteSource source;
    uint32_t size = 0;
    if (!scan_append_path(state, name, false)) {
        return false;
    }
    bool opened = files->open_file(files->context, state->path, &source, &size);
    if (opened) {
        cue_sheet_open(&state->cue, source, size);
    }
    return opened;
}

static uint32_t cue_target(ScanState *state, const ScanFileSystem *files, const Level *level,
                           uint32_t offset)
{
    uint32_t folder = state->path_length;
    const char *name = scan_entry_name(state, offset);
    if (!open_cue(state, files, name)) {
        scan_truncate_path(state, folder);
        return NO_TARGET;
    }
    bool usable = cue_sheet_usable(&state->cue);
    files->close_file(files->context);
    scan_truncate_path(state, folder);
    return usable ? resolve_target(state, level, name) : NO_TARGET;
}

ScanResult scan_mark_cue_targets(ScanState *state, const ScanFileSystem *files, const Level *level)
{
    const uint32_t *offsets = level_offsets(state, level);
    for (uint32_t index = 0; index < level->count; index++) {
        uint32_t offset = offsets[index];
        if (state->listing[offset] == ENTRY_DIRECTORY ||
            !scan_is_cue_name(scan_entry_name(state, offset))) {
            continue;
        }
        uint32_t target = cue_target(state, files, level, offset);
        if (target != NO_TARGET) {
            state->listing[target] = ENTRY_CLAIMED;
        }
    }
    return SCAN_OK;
}

static bool copy_audio_path(ScanState *state, const char *name)
{
    uint32_t length = scan_text_length(name);
    if (state->path_length + length + 1 > SCAN_PATH_CAPACITY) {
        return false;
    }
    for (uint32_t index = 0; index < state->path_length; index++) {
        state->audio_path[index] = state->path[index];
    }
    for (uint32_t index = 0; index <= length; index++) {
        state->audio_path[state->path_length + index] = name[index];
    }
    return true;
}

static bool read_audio_tags(ScanState *state, const ScanJob *job)
{
    ByteSource source;
    uint32_t size = 0;
    if (!job->files.open_file(job->files.context, state->audio_path, &source, &size)) {
        return false;
    }
    TagFormat format = track_tags_format_for_name(state->audio_path);
    bool read = track_tags_read(source, size, format, &state->tags);
    job->files.close_file(job->files.context);
    track_fields_from_tags(state->audio_path + 1, &state->tags, &state->fields);
    return read;
}

static uint32_t duration_between(uint32_t start, uint32_t end, uint32_t file_duration)
{
    uint32_t limit = end != 0 ? end : file_duration;
    return limit > start ? limit - start : 0;
}

static void track_title(const CueEntry *entry, char *text)
{
    StringBuilder builder = string_builder_make(text, CUE_TRACK_TITLE);
    string_builder_append(&builder, "Track ");
    string_builder_append_unsigned(&builder, entry->number);
}

static const char *non_empty(const char *text, const char *fallback)
{
    return text[0] != '\0' ? text : fallback;
}

static void apply_entry(const ScanState *state, const CueEntry *entry, char *title,
                        BuilderTrack *track)
{
    const CueAlbum *album = &state->cue.album;
    track_title(entry, title);
    track->title = non_empty(entry->title, title);
    track->artist = non_empty(entry->performer, non_empty(album->performer, track->artist));
    track->album_artist_guessed = track->album_artist_guessed && album->performer[0] == '\0';
    track->album_artist = non_empty(album->performer, track->album_artist);
    track->album = non_empty(album->title, track->album);
    track->genre = non_empty(album->genre, track->genre);
    uint16_t year = (uint16_t)track_fields_year(album->date);
    track->year = year != 0 ? year : track->year;
    track->number = (uint8_t)(entry->number < SMALL_MAX ? entry->number : SMALL_MAX);
    track->start_ms = entry->start_ms;
    track->end_ms = entry->end_ms;
    track->duration_ms = duration_between(entry->start_ms, entry->end_ms, track->duration_ms);
}

static ScanResult add_entry(ScanState *state, const CueEntry *entry, const char *cue_name)
{
    char title[CUE_TRACK_TITLE];
    BuilderTrack track = state->fields.track;
    apply_entry(state, entry, title, &track);
    track.sheet = cue_name;
    return scan_builder_result(library_builder_add_track(&state->builder, &track));
}

static ScanResult add_entries(ScanState *state, const ScanJob *job, ScanStats *stats,
                              const char *cue_name)
{
    ScanResult result = SCAN_OK;
    const CueEntry *entry = cue_sheet_next(&state->cue);
    while (result == SCAN_OK && entry != NULL) {
        result = add_entry(state, entry, cue_name);
        stats->tracks += result == SCAN_OK;
        scan_report(job, stats);
        entry = cue_sheet_next(&state->cue);
    }
    return result;
}

static ScanResult add_from_cue(ScanState *state, const ScanJob *job, ScanStats *stats,
                               const char *cue_name)
{
    uint32_t folder = state->path_length;
    ScanResult result = SCAN_OK;
    if (open_cue(state, &job->files, cue_name)) {
        result = add_entries(state, job, stats, cue_name);
        job->files.close_file(job->files.context);
    }
    scan_truncate_path(state, folder);
    return result;
}

ScanResult scan_add_cue(ScanState *state, const ScanJob *job, ScanStats *stats, uint32_t offset)
{
    const Level *level = &state->levels[state->depth - 1];
    uint32_t target = cue_target(state, &job->files, level, offset);
    stats->files++;
    if (target == NO_TARGET) {
        return SCAN_OK;
    }
    if (!copy_audio_path(state, scan_entry_name(state, target)) || !read_audio_tags(state, job)) {
        stats->skipped++;
        return SCAN_OK;
    }
    return add_from_cue(state, job, stats, scan_entry_name(state, offset));
}
