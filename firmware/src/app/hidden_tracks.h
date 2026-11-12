#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library_store.h"
#include "playback/mix_identity.h"
#include "player.h"
#include "ui/choice_screen.h"

enum {
    HIDDEN_TRACKS_LABEL_BYTES = 30
};

typedef struct HiddenTracks {
    LibraryStore *store;
    Player *player;
} HiddenTracks;

typedef enum HiddenResult {
    HIDDEN_DONE,
    HIDDEN_FULL,
    HIDDEN_REFUSED,
    HIDDEN_FAILED,
    HIDDEN_RESCAN_FIRST
} HiddenResult;

typedef struct HiddenTracksChoice {
    ChoiceScreen choice;
    const char *options[HIDDEN_TRACKS_RECORDS];
    char labels[HIDDEN_TRACKS_RECORDS][HIDDEN_TRACKS_LABEL_BYTES];
} HiddenTracksChoice;

uint16_t hidden_tracks_read_head(HiddenTracksHead *head);
bool hidden_tracks_read(uint16_t record, MixIdentity *identity);
bool hidden_tracks_write(uint16_t record, const MixIdentity *identity);
bool hidden_tracks_prepare(HiddenTracks tracks, HiddenTracksHead *head);
bool hidden_tracks_save(HiddenTracks tracks, HiddenTracksHead *head);
void hidden_tracks_exclude(HiddenTracks tracks, uint16_t track);
void hidden_tracks_include(HiddenTracks tracks, uint16_t track);
uint16_t hidden_tracks_anchor(const Player *player);
void hidden_tracks_replan(Player *player, uint16_t anchor);
HiddenResult hidden_tracks_unhide(HiddenTracks tracks, uint16_t record);
void hidden_tracks_file_name(const char *path, char *name, size_t capacity);
