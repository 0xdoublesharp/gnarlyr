#include "playback/resume_rewind.h"

enum {
    REWIND_POINT_COUNT = 4,
    MS_PER_TENTH = 100
};

static const uint32_t PAUSE_TENTHS[REWIND_POINT_COUNT] = {100, 600, 6000, 36000};
static const uint32_t MUSIC_REWIND_MS[REWIND_POINT_COUNT] = {0, 3000, 10000,
                                                             RESUME_REWIND_COLD_MUSIC_MS};

static uint32_t music_rewind_ms(uint32_t paused_ms)
{
    uint32_t tenths = paused_ms / MS_PER_TENTH;
    if (tenths <= PAUSE_TENTHS[0]) {
        return 0;
    }
    for (int point = 1; point < REWIND_POINT_COUNT; point++) {
        if (tenths < PAUSE_TENTHS[point]) {
            uint32_t span = PAUSE_TENTHS[point] - PAUSE_TENTHS[point - 1];
            uint32_t rise = MUSIC_REWIND_MS[point] - MUSIC_REWIND_MS[point - 1];
            return MUSIC_REWIND_MS[point - 1] + (tenths - PAUSE_TENTHS[point - 1]) * rise / span;
        }
    }
    return MUSIC_REWIND_MS[REWIND_POINT_COUNT - 1];
}

uint32_t resume_rewind_ms(ResumeRewind rewind)
{
    uint32_t music = 0;
    if (rewind.pause == RESUME_REWIND_TIMED) {
        music = music_rewind_ms(rewind.paused_ms);
    } else if (rewind.pause == RESUME_REWIND_COLD) {
        music = RESUME_REWIND_COLD_MUSIC_MS;
    }
    return rewind.spoken ? music + music / 2 : music;
}

uint32_t resume_rewind_position(uint32_t position_ms, uint32_t rewind_ms)
{
    return position_ms > rewind_ms ? position_ms - rewind_ms : 0;
}
