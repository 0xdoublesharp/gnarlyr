#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum ResumeRewindPause {
    RESUME_REWIND_NONE,
    RESUME_REWIND_TIMED,
    RESUME_REWIND_COLD
} ResumeRewindPause;

enum {
    RESUME_REWIND_COLD_MUSIC_MS = 20000,
    RESUME_REWIND_COLD_SPOKEN_MS = 30000
};

typedef struct ResumeRewind {
    ResumeRewindPause pause;
    uint32_t paused_ms;
    bool spoken;
} ResumeRewind;

uint32_t resume_rewind_ms(ResumeRewind rewind);
uint32_t resume_rewind_position(uint32_t position_ms, uint32_t rewind_ms);
