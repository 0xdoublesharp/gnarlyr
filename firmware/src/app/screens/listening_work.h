#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/i18n.h"
#include "library/lyrics.h"
#include "listening_store.h"
#include "playback/listening_export.h"
#include "playback/listening_stats.h"
#include "playback/moments.h"
#include "screens/smart_list_work.h"
#include "theme_picker.h"
#include "ui/wire_motion.h"

enum {
    LISTENING_NAME_BYTES = 128,
    LISTENING_VALUE_BYTES = 32,
    LISTENING_TALLY_CAPACITY = 512,
    LYRICS_VISIBLE_LINES = 3,
    LYRICS_LINE_BYTES = 192
};

typedef struct ListeningWork {
    ListeningTop top;
    ListeningExport export_job;
    ListeningFile export_file;
    ListeningTally tallies[LISTENING_TALLY_CAPACITY];
    char names[LISTENING_TOP_COUNT][LISTENING_NAME_BYTES];
    char times[LISTENING_TOP_COUNT][LISTENING_VALUE_BYTES];
    StringId export_reason;
    uint8_t export_stage;
    bool named;
} ListeningWork;

typedef struct LyricsWork {
    LyricsIndex index;
    char lines[LYRICS_VISIBLE_LINES][LYRICS_LINE_BYTES];
    int center;
    bool timed;
    bool ready;
} LyricsWork;

typedef struct MomentsWork {
    Moments moments;
    MomentsJob job;
    Moment moment;
    MomentResolve resolve;
    char labels[THEME_MAX_ROWS][MOMENT_LABEL_BYTES];
    int first;
    StoreLoad load;
} MomentsWork;

typedef struct ListeningModuleWork {
    const void *owner;
    union {
        ListeningWork listening;
        SmartListWork smart;
        LyricsWork lyrics;
        MomentsWork moments;
    } as;
} ListeningModuleWork;

extern ListeningModuleWork listening_module_work;
