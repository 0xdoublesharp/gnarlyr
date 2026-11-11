#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/replay_gain.h"
#include "hal/audio.h"
#include "library/library.h"
#include "library/library_browse.h"
#include "playback/play_queue.h"
#include "playback/resume_rewind.h"
#include "playback/up_next.h"
#include "player_storage.h"
#include "queue_edits.h"

enum {
    PLAYER_DEFAULT_VOLUME = 13,
    PLAYER_RESTART_THRESHOLD_MS = 3000,
    PLAYER_SAVE_INTERVAL_MS = 15000,
    PLAYER_PAUSE_RELEASE_MS = 30000,
    PLAYER_PATH_CAPACITY = 260
};

typedef enum PlayerState {
    PLAYER_IDLE,
    PLAYER_PLAYING,
    PLAYER_PAUSED
} PlayerState;

typedef enum PlayerUpNextResult {
    PLAYER_UP_NEXT_ADDED,
    PLAYER_UP_NEXT_STARTED,
    PLAYER_UP_NEXT_FULL,
    PLAYER_UP_NEXT_SAVE_FAILED,
    PLAYER_UP_NEXT_CHANGED,
    PLAYER_UP_NEXT_FAILED,
    PLAYER_UP_NEXT_WORKING
} PlayerUpNextResult;

typedef enum PlayerQueueEditResult {
    PLAYER_QUEUE_EDITED,
    PLAYER_QUEUE_EDIT_INVALID,
    PLAYER_QUEUE_EDIT_STALE,
    PLAYER_QUEUE_EDIT_SAVE_FAILED,
    PLAYER_QUEUE_EDIT_WORKING,
    PLAYER_QUEUE_EDIT_CANCELLED
} PlayerQueueEditResult;

typedef enum PlayerNextSource {
    PLAYER_NEXT_FROM_QUEUE,
    PLAYER_NEXT_FROM_UP_NEXT,
    PLAYER_NEXT_SAME_SONG
} PlayerNextSource;

typedef struct PlayerStatus {
    PlayerState state;
    bool has_track;
    uint16_t track;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint8_t volume;
    ShuffleMode shuffle;
    RepeatMode repeat;
    PlayQueuePlace place;
    HalAudioFormat format;
    bool spoken;
} PlayerStatus;

typedef struct PlayerResume {
    uint32_t (*position_for)(void *context, uint16_t track);
    void *context;
} PlayerResume;

typedef struct PlayerListStart {
    BrowseQuery source;
    uint32_t position;
    uint32_t seed;
    bool in_order;
    uint32_t position_ms;
} PlayerListStart;

typedef struct PlayerQueueJob {
    PlayQueueStream stream;
    TrackOrder pending;
    TrackOrderJob writer;
    SessionState session;
    TrackOrderEdit edit;
    PlayerQueueEditResult result;
    uint32_t serial;
    uint16_t row;
    uint16_t current;
    uint16_t count;
    uint8_t stage;
    UpNextEdit action;
    bool snapshot;
    bool insert;
    bool next;
    bool resume;
} PlayerQueueJob;

typedef struct Player {
    const Library *library;
    uint32_t fingerprint;
    PlayQueue queue;
    PlayerStorage storage;
    CassetteState cassette;
    PlayerQueueJob editing;
    UpNext up_next;
    uint32_t up_next_revision;
    bool from_up_next;
    RepeatMode repeat;
    ShuffleMode shuffle;
    uint8_t volume;
    uint8_t replay_gain_mode;
    bool prevent_clipping;
    bool fade_transitions;
    uint8_t transition_tenths;
    PlayerState state;
    bool has_track;
    bool loaded;
    uint16_t track;
    uint32_t paused_position_ms;
    uint32_t start_ms;
    uint32_t token_counter;
    uint32_t current_token;
    bool has_next;
    uint16_t next_track;
    uint32_t next_token;
    uint32_t next_start_ms;
    PlayQueueCursor next_cursor;
    PlayerNextSource next_source;
    uint32_t last_save_ms;
    bool pause_timing;
    bool standby_held;
    uint32_t paused_since_ms;
    bool rewind_on_resume;
    ResumeRewindPause rewind_pause;
    uint32_t paused_at_ms;
    HalAudioJack jack;
    uint32_t refusal_count;
    HalAudioRefusal refusal;
    bool spoken;
    PlayerResume resume;
} Player;

extern const char PLAYER_RESUME_PATH[];
extern const char PLAYER_UP_NEXT_PATH[];

void player_init(Player *player, const Library *library);
bool player_play_list(Player *player, BrowseQuery source, uint32_t position, uint32_t seed);
bool player_play_from(Player *player, PlayerListStart start);
bool player_use_order(Player *player, const TrackOrder *order, uint16_t position);
bool player_use_tape(Player *player, const TrackOrder *order, uint16_t position,
                     const CassetteState *cassette);
void player_set_resume(Player *player, PlayerResume resume);
void player_set_rewind(Player *player, bool on);
void player_jump(Player *player, int32_t delta_ms);
void player_toggle_pause(Player *player);
void player_skip(Player *player, int direction);
void player_seek(Player *player, uint32_t position_ms);
void player_change_volume(Player *player, int delta);
void player_set_shuffle(Player *player, ShuffleMode shuffle, uint32_t seed);
void player_set_repeat(Player *player, RepeatMode repeat);
PlayerUpNextResult player_play_next(Player *player, uint16_t track);
PlayerUpNextResult player_add_to_up_next(Player *player, uint16_t track);
PlayerQueueEditResult player_edit_up_next(Player *player, uint32_t revision, uint16_t index,
                                          UpNextEdit edit);
bool player_jump_to_up_next(Player *player, uint16_t index);
bool player_jump_to_queue(Player *player, PlayQueueCursor cursor);
PlayerQueueEditResult player_edit_queue(Player *player, uint32_t revision, uint16_t row,
                                        UpNextEdit edit);
PlayerQueueEditResult player_snapshot_queue(Player *player);
bool player_queue_busy(const Player *player);
void player_queue_cancel(Player *player);
void player_update(Player *player, uint32_t now_ms);
void player_refusal_text(HalAudioRefusal refusal, char *text, size_t capacity);
bool player_save(Player *player);
void player_release_output(Player *player);
void player_hold_standby(Player *player, bool held);
bool player_in_standby(const Player *player);
bool player_restore(Player *player);
PlayerStatus player_status(const Player *player);

void player_set_replay_gain(Player *player, ReplayGainMode mode, bool prevent_clipping);
void player_set_transitions(Player *player, bool fade, uint8_t tenths);
