#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/i18n.h"
#include "hal/storage.h"
#include "library/lyrics.h"
#include "listening_store.h"
#include "playback/listening_export.h"
#include "playback/listening_recap.h"
#include "playback/listening_stats.h"
#include "playback/moments.h"
#include "screens/smart_list_work.h"
#include "ui/wire_motion.h"
#include "theme_picker.h"

enum {
    LISTENING_NAME_BYTES = 128,
    LISTENING_VALUE_BYTES = 32,
    LISTENING_TALLY_CAPACITY = 512,
    LYRICS_VISIBLE_LINES = 3,
    LYRICS_LINE_BYTES = 192,
    RECAP_NAME_BYTES = 96,
    RECAP_NAMED_GROUPS = 2
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

typedef struct RecapWork {
    ListeningRecapJob job;
    ListeningLog log;
    HalFile handle;
    ListeningTally tallies[LISTENING_TALLY_CAPACITY];
    ListeningFile cache;
    WireMotion still;
    char names[LISTENING_RECAP_GROUPS][LISTENING_RECAP_TOP][RECAP_NAME_BYTES];
    char artists[RECAP_NAMED_GROUPS][LISTENING_RECAP_TOP][RECAP_NAME_BYTES];
    uint16_t albums[LISTENING_RECAP_TOP];
    uint8_t covers;
    uint8_t stage;
    bool flourished;
} RecapWork;

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
        RecapWork recap;
        ThemeWork themes;
        SmartListWork smart;
        MomentsWork moments;
    } as;
} ListeningModuleWork;

extern ListeningModuleWork listening_module_work;
