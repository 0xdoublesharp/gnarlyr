#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SCAN_GUARD_OFFSET = 512,
    SCAN_GUARD_MAGIC = 0x47535344,
    SCAN_GUARD_RUNNING_BYTE = 4,
    SCAN_GUARD_ATTEMPTS_BYTE = 5,
    SCAN_GUARD_SKIPS = 4,
    SCAN_GUARD_BATCH = 8,
    SCAN_GUARD_RETRIES = 3,
    SCAN_GUARD_PHASE_MASK = 7
};

typedef enum ScanPhase {
    SCAN_PHASE_NONE,
    SCAN_PHASE_BATCH,
    SCAN_PHASE_TAGS,
    SCAN_PHASE_CUE,
    SCAN_PHASE_CHAPTERS,
    SCAN_PHASE_COVER
} ScanPhase;

typedef enum ScanGuardStep {
    SCAN_GUARD_GO,
    SCAN_GUARD_SAVE,
    SCAN_GUARD_SKIP
} ScanGuardStep;

typedef struct ScanGuard {
    uint32_t magic;
    uint8_t running;
    uint8_t attempts;
    uint8_t next_skip;
    uint8_t unused;
    uint32_t mark;
    uint32_t careful;
    uint32_t skips[SCAN_GUARD_SKIPS];
    uint32_t seen;
    uint8_t batch_left;
    uint8_t careful_left;
} ScanGuard;

enum {
    SCAN_GUARD_BYTES = offsetof(ScanGuard, seen)
};

_Static_assert(offsetof(ScanGuard, running) == SCAN_GUARD_RUNNING_BYTE, "running byte");
_Static_assert(offsetof(ScanGuard, attempts) == SCAN_GUARD_ATTEMPTS_BYTE, "attempts byte");
_Static_assert(SCAN_GUARD_BYTES == 32, "record layout");

void scan_guard_begin(ScanGuard *guard, uint32_t stops);
uint32_t scan_guard_mark(const char *path, uint32_t size, bool covers);
ScanGuardStep scan_guard_open(ScanGuard *guard, uint32_t mark);

static inline bool scan_guard_covers_allowed(const ScanGuard *guard)
{
    return guard->attempts <= SCAN_GUARD_RETRIES;
}
