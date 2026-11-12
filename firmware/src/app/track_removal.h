#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "browse_screen.h"
#include "hidden_tracks.h"
#include "ui/screen.h"
#include "ui/toast.h"

typedef struct TrackRemoval {
    HiddenTracks tracks;
    SongActions *actions;
    Toast *toast;
} TrackRemoval;

Screen *track_removal_manage(TrackRemoval removal, uint16_t track, SongManage manage);
HiddenResult track_removal_hide(HiddenTracks tracks, uint16_t track);
HiddenResult track_removal_delete(HiddenTracks tracks, uint16_t track);
bool track_removal_path_allowed(const char *path);
