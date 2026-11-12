#include "scan_guard.h"

#include "core/hash.h"
#include "library/library_scan.h"
#include "library/track_tags.h"

enum {
    SKIP_MASK = SCAN_GUARD_SKIPS - 1
};

_Static_assert((SCAN_GUARD_SKIPS & SKIP_MASK) == 0, "the skip list is a power of two");

static uint32_t phase_of(uint32_t mark)
{
    return mark & SCAN_GUARD_PHASE_MASK;
}

static void remember_stop(ScanGuard *guard)
{
    if (phase_of(guard->mark) == SCAN_PHASE_BATCH) {
        guard->careful = guard->mark;
    } else if (guard->mark != 0) {
        guard->skips[guard->next_skip++ & SKIP_MASK] = guard->mark;
    }
}

void scan_guard_begin(ScanGuard *guard, uint32_t stops)
{
    if (guard->magic == SCAN_GUARD_MAGIC && guard->running) {
        remember_stop(guard);
    }
    guard->magic = SCAN_GUARD_MAGIC;
    guard->attempts = (uint8_t)(stops + 1);
    guard->running = 1;
    guard->mark = 0;
}

static ScanPhase phase_for(const char *path, bool covers)
{
    if (covers) {
        return SCAN_PHASE_COVER;
    }
    if (scan_is_cue_name(path)) {
        return SCAN_PHASE_CUE;
    }
    if (scan_is_chaptered_name(path)) {
        return SCAN_PHASE_CHAPTERS;
    }
    return track_tags_format_for_name(path) == TAG_FORMAT_UNKNOWN ? SCAN_PHASE_TAGS
                                                                  : SCAN_PHASE_BATCH;
}

uint32_t scan_guard_mark(const char *path, uint32_t size, bool covers)
{
    uint32_t key = HASH_START;
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        key = (key ^ (uint8_t)*cursor) * HASH_PRIME;
    }
    key = (key ^ size) * HASH_PRIME;
    return (key & ~(uint32_t)SCAN_GUARD_PHASE_MASK) | phase_for(path, covers);
}

static uint32_t matching_skip(const ScanGuard *guard, uint32_t mark)
{
    for (int index = 0; index < SCAN_GUARD_SKIPS; index++) {
        uint32_t skip = guard->skips[index];
        bool same = skip != 0 && ((skip ^ mark) & ~(uint32_t)SCAN_GUARD_PHASE_MASK) == 0;
        if (same && (phase_of(skip) != SCAN_PHASE_COVER || phase_of(mark) == SCAN_PHASE_COVER)) {
            return skip;
        }
    }
    return 0;
}

static uint32_t batch_mark(ScanGuard *guard, uint32_t mark)
{
    if (mark == guard->careful) {
        guard->careful_left = SCAN_GUARD_BATCH;
    }
    if (guard->careful_left > 0) {
        guard->careful_left--;
        return (mark & ~(uint32_t)SCAN_GUARD_PHASE_MASK) | SCAN_PHASE_TAGS;
    }
    if (guard->batch_left > 0) {
        guard->batch_left--;
        return 0;
    }
    guard->batch_left = SCAN_GUARD_BATCH - 1;
    return mark;
}

ScanGuardStep scan_guard_open(ScanGuard *guard, uint32_t mark)
{
    guard->seen = matching_skip(guard, mark);
    if (guard->seen != 0) {
        return SCAN_GUARD_SKIP;
    }
    if (phase_of(mark) == SCAN_PHASE_BATCH) {
        mark = batch_mark(guard, mark);
    } else {
        guard->batch_left = 0;
    }
    if (mark == 0) {
        return SCAN_GUARD_GO;
    }
    guard->mark = mark;
    return SCAN_GUARD_SAVE;
}
