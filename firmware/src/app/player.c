#include "player.h"
#include "player_private.h"

#include <stddef.h>

#include "core/format.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "library/library_gain.h"
#include "playback/mix_identity.h"
#include "playback/resume_state.h"
#include "playback/spoken.h"
#include "playback/track_transition.h"

const char PLAYER_RESUME_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/resume.bin";
const char PLAYER_UP_NEXT_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/upnext.bin";

typedef struct TrackAudio {
    char path[PLAYER_PATH_CAPACITY];
    HalAudioTrack track;
} TrackAudio;

static uint32_t position_in_track(const Player *player, const HalAudioStatus *status)
{
    bool upcoming = player->has_next && status->token == player->next_token;
    uint32_t start = upcoming ? player->next_start_ms : player->start_ms;
    return status->position_ms > start ? status->position_ms - start : 0;
}

static uint32_t current_position(const Player *player)
{
    if (!player->loaded) {
        return player->paused_position_ms;
    }
    HalAudioStatus status = hal_audio_status();
    return position_in_track(player, &status);
}

static bool track_audio(const Player *player, uint16_t track, TrackAudio *audio)
{
    LibraryTrack record;
    char relative[PLAYER_PATH_CAPACITY];
    if (!library_read_track(player->library, track, &record) ||
        !library_read_string(player->library, record.path, relative, sizeof relative)) {
        return false;
    }
    StringBuilder builder = string_builder_make(audio->path, sizeof audio->path);
    string_builder_append_char(&builder, '/');
    string_builder_append(&builder, relative);
    audio->track =
        (HalAudioTrack){.path = audio->path,
                        .start_ms = record.start_ms,
                        .end_ms = record.end_ms,
                        .token = 0,
                        .replay_gain = library_replay_gain_for(
                            player->library, track, (ReplayGainMode)player->replay_gain_mode,
                            player->prevent_clipping)};
    return !builder.overflowed;
}

static uint32_t new_token(Player *player)
{
    return ++player->token_counter;
}

static void become_idle(Player *player)
{
    player->state = PLAYER_IDLE;
    player->loaded = false;
    player->has_next = false;
    player->paused_position_ms = 0;
}

static bool store_up_next(Player *player, const UpNext *up_next)
{
    SessionState state = player_session_state(player, up_next);
    return player_store_session(player, &state);
}

static void save_up_next(Player *player)
{
    player->up_next_revision++;
    (void)store_up_next(player, &player->up_next);
}

static bool current_track(const Player *player, uint16_t *track)
{
    if (player->from_up_next) {
        *track = player->track;
        return true;
    }
    return play_queue_current(&player->queue, track);
}

static bool upcoming_track(Player *player, PlayQueueCursor *cursor, uint16_t *track)
{
    if (player->repeat == REPEAT_ONE && player->from_up_next) {
        player->next_source = PLAYER_NEXT_SAME_SONG;
        *track = player->track;
        return true;
    }
    if (player->repeat != REPEAT_ONE && up_next_first(&player->up_next, track)) {
        player->next_source = PLAYER_NEXT_FROM_UP_NEXT;
        return true;
    }
    player->next_source = PLAYER_NEXT_FROM_QUEUE;
    return play_queue_upcoming(&player->queue, player->repeat, cursor) &&
           play_queue_track_at(&player->queue, *cursor, track);
}

static uint32_t transition_ms(const Player *player, uint16_t next, const char *next_path)
{
    TransitionRequest request = {.fade = player->fade_transitions,
                                 .tenths = player->transition_tenths,
                                 .shuffle = player->shuffle,
                                 .from = {.spoken = player->spoken},
                                 .to = {.spoken = spoken_kind_of(next_path) != SPOKEN_NONE}};
    if (!request.fade ||
        !library_read_track(player->library, player->track, &request.from.record) ||
        !library_read_track(player->library, next, &request.to.record)) {
        return 0;
    }
    return track_transition_fade_ms(&request);
}

static bool prepare_next(Player *player, TrackAudio *audio)
{
    PlayQueueCursor upcoming = player->queue.cursor;
    uint16_t track = 0;
    player->has_next =
        upcoming_track(player, &upcoming, &track) && track_audio(player, track, audio);
    if (!player->has_next) {
        return false;
    }
    player->next_cursor = upcoming;
    player->next_track = track;
    player->next_token = new_token(player);
    player->next_start_ms = audio->track.start_ms;
    audio->track.token = player->next_token;
    audio->track.fade_ms = transition_ms(player, track, audio->path);
    return true;
}

void player_requeue(Player *player)
{
    if (!player->loaded) {
        return;
    }
    TrackAudio audio;
    hal_audio_queue_next(prepare_next(player, &audio) ? &audio.track : NULL);
}

static void note_spoken(Player *player, uint16_t track)
{
    TrackAudio audio;
    player->spoken =
        track_audio(player, track, &audio) && spoken_kind_of(audio.path) != SPOKEN_NONE;
}

static uint32_t resume_point(const Player *player, uint16_t track, uint32_t start_ms)
{
    if (start_ms != 0 || !player->spoken || player->resume.position_for == NULL) {
        return start_ms;
    }
    return player->resume.position_for(player->resume.context, track);
}

static bool start_track(Player *player, uint32_t start_ms, bool from_bookmark)
{
    TrackAudio audio;
    uint16_t track = 0;
    player->rewind_pause = RESUME_REWIND_NONE;
    if (!current_track(player, &track) || !track_audio(player, track, &audio)) {
        become_idle(player);
        return false;
    }
    player->spoken = spoken_kind_of(audio.path) != SPOKEN_NONE;
    start_ms = from_bookmark ? resume_point(player, track, start_ms) : start_ms;
    player->track = track;
    player->has_track = true;
    player->current_token = new_token(player);
    player->up_next_revision++;
    audio.track.token = player->current_token;
    player->start_ms = audio.track.start_ms;
    TrackAudio next;
    bool queued = prepare_next(player, &next);
    player->loaded = hal_audio_play(&audio.track, start_ms, queued ? &next.track : NULL);
    if (!player->loaded) {
        player->refusal = hal_audio_refusal();
        player->refusal_count++;
        become_idle(player);
        return false;
    }
    player->state = PLAYER_PLAYING;
    return true;
}

static bool start_current(Player *player, uint32_t start_ms)
{
    return start_track(player, start_ms, true);
}

static void note_pause(Player *player)
{
    player->rewind_pause = RESUME_REWIND_TIMED;
    player->paused_at_ms = hal_clock_milliseconds();
}

static uint32_t rewind_due(const Player *player)
{
    ResumeRewind rewind = {.pause = player->rewind_pause,
                           .paused_ms = hal_clock_milliseconds() - player->paused_at_ms,
                           .spoken = player->spoken};
    return player->rewind_on_resume ? resume_rewind_ms(rewind) : 0;
}

static void resume_paused(Player *player)
{
    uint32_t back = rewind_due(player);
    player->rewind_pause = RESUME_REWIND_NONE;
    if (!player->loaded) {
        start_track(player, resume_rewind_position(player->paused_position_ms, back), back == 0);
        return;
    }
    uint32_t position = current_position(player);
    uint32_t target = resume_rewind_position(position, back);
    if (target != position) {
        hal_audio_seek(player->start_ms + target);
    }
    hal_audio_resume();
    player->state = PLAYER_PLAYING;
}

static void restart_track(Player *player)
{
    player->rewind_pause = RESUME_REWIND_NONE;
    if (player->loaded) {
        hal_audio_seek(player->start_ms);
    } else {
        player->paused_position_ms = 0;
    }
}

static bool start_playable(Player *player, uint32_t start_ms, int direction, bool wrap)
{
    if (start_current(player, start_ms)) {
        return true;
    }
    for (uint16_t tries = 1; tries < player->queue.count; tries++) {
        PlayQueueCursor target;
        if (!play_queue_neighbor(&player->queue, direction, wrap, &target)) {
            break;
        }
        play_queue_move_to(&player->queue, target);
        if (start_current(player, 0)) {
            return true;
        }
    }
    become_idle(player);
    return false;
}

static bool start_up_next(Player *player)
{
    uint16_t track = 0;
    bool started = false;
    while (!started && up_next_first(&player->up_next, &track)) {
        up_next_drop(&player->up_next, 1);
        player->from_up_next = true;
        player->track = track;
        started = start_current(player, 0);
    }
    if (!started) {
        player->from_up_next = false;
    }
    save_up_next(player);
    return started;
}

static bool move_and_start(Player *player, int direction, bool wrap)
{
    if (direction > 0 && player->up_next.count > 0 && start_up_next(player)) {
        return true;
    }
    if (direction < 0 && player->from_up_next) {
        player->from_up_next = false;
        return start_playable(player, 0, direction, wrap);
    }
    PlayQueueCursor target;
    if (!play_queue_neighbor(&player->queue, direction, wrap, &target)) {
        return false;
    }
    player->from_up_next = false;
    play_queue_move_to(&player->queue, target);
    return start_playable(player, 0, direction, wrap);
}

void player_init(Player *player, const Library *library)
{
    *player = (Player){0};
    player_storage_init(&player->storage);
    player->library = library;
    player->fingerprint = library_fingerprint(library);
    player->queue.library = library;
    player->volume = PLAYER_DEFAULT_VOLUME;
    player->prevent_clipping = true;
    player->jack = hal_audio_jack();
    hal_audio_set_volume(player->volume);
}

bool player_use_order(Player *player, const TrackOrder *order, uint16_t position)
{
    uint16_t track;
    MixIdentity identity;
    PlayQueue queue;
    if (order == NULL || position >= order->count ||
        !track_order_read(order, position, 1, &track) ||
        mix_identity_read(player->library, track, &identity) != STORE_READY) {
        return false;
    }
    PlayQueuePlan plan = {.source = browse_root_query(BROWSE_ALL_SONGS),
                          .anchor_track = track,
                          .shuffle = player->shuffle};
    if (!play_queue_use_order(&queue, player->library, order, plan)) {
        return false;
    }
    PlayQueueCursor cursor = {.position = position};
    play_queue_move_to(&queue, cursor);
    SessionState state = player_session_state(player, &player->up_next);
    state.queue_kind = SESSION_QUEUE_STORED;
    state.order_generation = order->generation;
    state.resume.plan = plan;
    state.resume.cursor = cursor;
    state.resume.track = track;
    state.resume.position_ms = 0;
    state.resume.from_up_next = false;
    up_next_clear(&state.up_next);
    if (!player_store_session(player, &state)) {
        return false;
    }
    player_queue_cancel(player);
    hal_audio_release();
    player->storage.order = *order;
    queue.stored_order = &player->storage.order;
    player->queue = queue;
    up_next_clear(&player->up_next);
    player->from_up_next = false;
    player->has_track = true;
    player->loaded = false;
    player->has_next = false;
    player->pause_timing = false;
    player->track = track;
    player->start_ms = identity.start_ms;
    player->paused_position_ms = 0;
    player->rewind_pause = RESUME_REWIND_NONE;
    player->spoken = spoken_kind_of(identity.path) != SPOKEN_NONE;
    player->state = PLAYER_PAUSED;
    player->up_next_revision++;
    return true;
}

void player_set_resume(Player *player, PlayerResume resume)
{
    player->resume = resume;
}

void player_set_rewind(Player *player, bool on)
{
    player->rewind_on_resume = on;
}

bool player_play_list(Player *player, BrowseQuery source, uint32_t position, uint32_t seed)
{
    PlayerListStart start = {
        .source = source, .position = position, .seed = seed, .in_order = false};
    return player_play_from(player, start);
}

bool player_play_from(Player *player, PlayerListStart start)
{
    BrowseItem item;
    if (!browse_item(player->library, start.source, start.position, &item) ||
        item.kind != BROWSE_ITEM_TRACK) {
        return false;
    }
    player_queue_cancel(player);
    player->from_up_next = false;
    PlayQueuePlan plan = {.source = start.source,
                          .anchor_track = item.index,
                          .shuffle = start.in_order ? SHUFFLE_OFF : player->shuffle,
                          .seed = start.seed};
    if (!play_queue_build(&player->queue, player->library, plan) ||
        !start_playable(player, 0, 1, player->repeat == REPEAT_ALL)) {
        return false;
    }
    player_save(player);
    return true;
}

void player_toggle_pause(Player *player)
{
    player_queue_cancel(player);
    switch (player->state) {
    case PLAYER_PLAYING:
        hal_audio_pause();
        player->state = PLAYER_PAUSED;
        note_pause(player);
        player_save(player);
        return;
    case PLAYER_PAUSED:
        resume_paused(player);
        return;
    case PLAYER_IDLE:
        if (player->has_track) {
            start_current(player, 0);
        }
        return;
    }
}

static uint32_t track_duration(const Player *player)
{
    LibraryTrack record;
    return library_read_track(player->library, player->track, &record) ? record.duration_ms : 0;
}

void player_jump(Player *player, int32_t delta_ms)
{
    player_queue_cancel(player);
    if (!player->has_track) {
        return;
    }
    uint32_t position = current_position(player);
    uint32_t duration = track_duration(player);
    if (delta_ms < 0) {
        uint32_t back = (uint32_t)-delta_ms;
        player_seek(player, position > back ? position - back : 0);
        return;
    }
    uint32_t target = position + (uint32_t)delta_ms;
    if (duration > 0 && target >= duration) {
        move_and_start(player, 1, player->repeat != REPEAT_OFF);
        return;
    }
    player_seek(player, target);
}

void player_skip(Player *player, int direction)
{
    player_queue_cancel(player);
    if (!player->has_track) {
        return;
    }
    if (player->spoken) {
        player_jump(player, direction > 0 ? SPOKEN_JUMP_FORWARD_MS : -SPOKEN_JUMP_BACK_MS);
        return;
    }
    if (direction < 0 && current_position(player) > PLAYER_RESTART_THRESHOLD_MS) {
        restart_track(player);
        return;
    }
    bool wrap = direction > 0 ? player->repeat != REPEAT_OFF : player->repeat == REPEAT_ALL;
    if (!move_and_start(player, direction, wrap) && direction < 0) {
        restart_track(player);
    }
}

void player_seek(Player *player, uint32_t position_ms)
{
    player_queue_cancel(player);
    if (!player->has_track) {
        return;
    }
    player->rewind_pause = RESUME_REWIND_NONE;
    if (player->loaded) {
        hal_audio_seek(player->start_ms + position_ms);
    } else {
        player->paused_position_ms = position_ms;
    }
}

void player_change_volume(Player *player, int delta)
{
    int level = player->volume + delta;
    if (level < 0) {
        level = 0;
    }
    if (level > HAL_AUDIO_VOLUME_MAX) {
        level = HAL_AUDIO_VOLUME_MAX;
    }
    player->volume = headphone_audio_limit(hal_audio_headphones(), (uint8_t)level);
    hal_audio_set_volume(player->volume);
}

void player_set_shuffle(Player *player, ShuffleMode shuffle, uint32_t seed)
{
    player_queue_cancel(player);
    player->shuffle = shuffle;
    if (!player->has_track) {
        return;
    }
    if (player->queue.stored_order != NULL) {
        player->queue.plan.shuffle = shuffle;
        player_save(player);
        return;
    }
    PlayQueuePlan plan = player->queue.plan;
    if (!play_queue_current(&player->queue, &plan.anchor_track)) {
        plan.anchor_track = player->track;
    }
    plan.shuffle = player->spoken ? SHUFFLE_OFF : shuffle;
    plan.seed = seed;
    if (play_queue_build(&player->queue, player->library, plan) && player->loaded) {
        player_requeue(player);
    }
    player_save(player);
}

void player_set_repeat(Player *player, RepeatMode repeat)
{
    player_queue_cancel(player);
    player->repeat = repeat;
    if (player->loaded) {
        player_requeue(player);
    }
}

static void take_next(Player *player)
{
    switch (player->next_source) {
    case PLAYER_NEXT_FROM_UP_NEXT:
        up_next_first(&player->up_next, &player->track);
        up_next_drop(&player->up_next, 1);
        player->from_up_next = true;
        save_up_next(player);
        return;
    case PLAYER_NEXT_SAME_SONG:
        return;
    case PLAYER_NEXT_FROM_QUEUE:
        break;
    }
    play_queue_move_to(&player->queue, player->next_cursor);
    play_queue_current(&player->queue, &player->track);
    player->from_up_next = false;
}

static void take_next_and_note(Player *player)
{
    take_next(player);
    note_spoken(player, player->track);
}

static void advance_to_next(Player *player)
{
    player->up_next_revision++;
    take_next_and_note(player);
    player->current_token = player->next_token;
    player->start_ms = player->next_start_ms;
    player->has_next = false;
    player_requeue(player);
    player_save(player);
}

static void finish_track(Player *player, bool failed)
{
    if (failed && move_and_start(player, 1, player->repeat != REPEAT_OFF)) {
        return;
    }
    become_idle(player);
    player_save(player);
}

bool player_queue_is_current(const Player *player)
{
    if (!player->loaded) {
        return true;
    }
    HalAudioStatus status = hal_audio_status();
    return status.token == player->current_token && status.state != HAL_AUDIO_STOPPED;
}

static PlayerQueueEditResult store_candidate(Player *player, const UpNext *candidate)
{
    if (!player_queue_is_current(player)) {
        return PLAYER_QUEUE_EDIT_STALE;
    }
    if (!store_up_next(player, candidate)) {
        return PLAYER_QUEUE_EDIT_SAVE_FAILED;
    }
    player->up_next = *candidate;
    player->up_next_revision++;
    if (player->loaded) {
        player_requeue(player);
    }
    return PLAYER_QUEUE_EDITED;
}

static PlayerQueueEditResult commit_up_next(Player *player, const UpNext *candidate)
{
    bool resume = player->loaded && player->state == PLAYER_PLAYING;
    if (resume) {
        hal_audio_pause();
    }
    PlayerQueueEditResult result = store_candidate(player, candidate);
    if (resume) {
        hal_audio_resume();
    }
    return result;
}

PlayerQueueEditResult player_edit_up_next(Player *player, uint32_t revision, uint16_t index,
                                          UpNextEdit edit)
{
    if (player_queue_busy(player) || revision != player->up_next_revision ||
        !player_queue_is_current(player)) {
        return PLAYER_QUEUE_EDIT_STALE;
    }
    UpNext candidate = player->up_next;
    if (!up_next_edit(&candidate, index, edit)) {
        return PLAYER_QUEUE_EDIT_INVALID;
    }
    return commit_up_next(player, &candidate);
}

static PlayerUpNextResult enqueue(Player *player, uint16_t track, bool next)
{
    if (player_queue_busy(player)) {
        return PLAYER_UP_NEXT_CHANGED;
    }
    if (!player->has_track) {
        player->queue = (PlayQueue){.library = player->library};
        player->from_up_next = true;
        player->track = track;
        return start_current(player, 0) ? PLAYER_UP_NEXT_STARTED : PLAYER_UP_NEXT_FAILED;
    }
    if (player->queue.stored_order != NULL) {
        return player_queue_insert(player, track, next);
    }
    if (!player_queue_is_current(player)) {
        return PLAYER_UP_NEXT_CHANGED;
    }
    UpNext candidate = player->up_next;
    bool added = next ? up_next_play_next(&candidate, track) : up_next_add(&candidate, track);
    if (!added) {
        return PLAYER_UP_NEXT_FULL;
    }
    PlayerQueueEditResult result = commit_up_next(player, &candidate);
    if (result == PLAYER_QUEUE_EDIT_STALE) {
        return PLAYER_UP_NEXT_CHANGED;
    }
    return result == PLAYER_QUEUE_EDITED ? PLAYER_UP_NEXT_ADDED : PLAYER_UP_NEXT_SAVE_FAILED;
}

PlayerUpNextResult player_play_next(Player *player, uint16_t track)
{
    return enqueue(player, track, true);
}

PlayerUpNextResult player_add_to_up_next(Player *player, uint16_t track)
{
    return enqueue(player, track, false);
}

bool player_jump_to_up_next(Player *player, uint16_t index)
{
    player_queue_cancel(player);
    if (index >= player->up_next.count) {
        return false;
    }
    up_next_drop(&player->up_next, index);
    bool started = start_up_next(player);
    player_save(player);
    return started;
}

bool player_jump_to_queue(Player *player, PlayQueueCursor cursor)
{
    player_queue_cancel(player);
    up_next_clear(&player->up_next);
    save_up_next(player);
    player->from_up_next = false;
    play_queue_move_to(&player->queue, cursor);
    bool started = start_playable(player, 0, 1, player->repeat == REPEAT_ALL);
    player_save(player);
    return started;
}

void player_release_output(Player *player)
{
    player_queue_cancel(player);
    if (!player->loaded) {
        return;
    }
    player->paused_position_ms = current_position(player);
    player_save(player);
    player->loaded = false;
    player->has_next = false;
    if (player->state == PLAYER_PLAYING) {
        player->state = PLAYER_PAUSED;
        note_pause(player);
    }
}

static void save_periodically(Player *player, uint32_t now_ms)
{
    if (player->state == PLAYER_PLAYING &&
        now_ms - player->last_save_ms >= PLAYER_SAVE_INTERVAL_MS) {
        player_save(player);
        player->last_save_ms = now_ms;
    }
}

static void follow_jack(Player *player)
{
    HalAudioJack jack = hal_audio_jack();
    bool unplugged = player->jack != HAL_AUDIO_JACK_NONE && jack == HAL_AUDIO_JACK_NONE;
    player->jack = jack;
    if (unplugged && player->state == PLAYER_PLAYING) {
        player_toggle_pause(player);
    }
}

void player_hold_standby(Player *player, bool held)
{
    player->standby_held = held;
}

bool player_in_standby(const Player *player)
{
    return player->state == PLAYER_PAUSED && player->has_track && !player->loaded;
}

static void release_long_pause(Player *player, uint32_t now_ms)
{
    if (player->state != PLAYER_PAUSED || player->standby_held) {
        player->pause_timing = false;
    } else if (!player->pause_timing) {
        player->pause_timing = true;
        player->paused_since_ms = now_ms;
    } else if (now_ms - player->paused_since_ms >= PLAYER_PAUSE_RELEASE_MS) {
        player_release_output(player);
        hal_audio_release();
        player->pause_timing = false;
    }
}

void player_update(Player *player, uint32_t now_ms)
{
    follow_jack(player);
    if (player_queue_busy(player)) {
        player_queue_step(player);
        return;
    }
    if (!player->loaded) {
        return;
    }
    HalAudioStatus status = hal_audio_status();
    if (player->has_next && status.token == player->next_token) {
        advance_to_next(player);
    } else if (status.state == HAL_AUDIO_STOPPED && status.token == player->current_token) {
        finish_track(player, status.failed);
    }
    save_periodically(player, now_ms);
    release_long_pause(player, now_ms);
}

SessionState player_session_state(const Player *player, const UpNext *up_next)
{
    SessionState state = {.resume = {.library_fingerprint = player->fingerprint,
                                     .plan = player->queue.plan,
                                     .repeat = player->repeat,
                                     .volume = player->volume,
                                     .cursor = player->queue.cursor,
                                     .track = player->track,
                                     .position_ms = current_position(player),
                                     .from_up_next = player->from_up_next},
                          .up_next = *up_next};
    if (player->queue.stored_order != NULL) {
        state.queue_kind = SESSION_QUEUE_STORED;
        state.order_generation = player->queue.stored_generation;
    } else if (player->queue.count == 0) {
        state.queue_kind = SESSION_QUEUE_EMPTY;
        state.resume.plan.source.group = LIBRARY_ARTISTS;
        state.resume.from_up_next = true;
    }
    return state;
}

bool player_store_session(Player *player, const SessionState *state)
{
    return player_storage_save(&player->storage, library_count(player->library, LIBRARY_TRACKS),
                               state) == STORE_DONE;
}

bool player_save(Player *player)
{
    player_queue_cancel(player);
    if (!player->has_track) {
        return false;
    }
    SessionState state = player_session_state(player, &player->up_next);
    return player_store_session(player, &state);
}

static bool read_legacy(const char *path, void *bytes, uint32_t length)
{
    HalFile file;
    if (!hal_file_open(&file, path)) {
        return false;
    }
    bool read = file.size >= length && hal_file_read(file, 0, bytes, length);
    hal_file_close(&file);
    return read;
}

static bool read_resume_state(ResumeState *state)
{
    uint8_t bytes[RESUME_STATE_SIZE];
    return read_legacy(PLAYER_RESUME_PATH, bytes, sizeof bytes) &&
           resume_state_decode(bytes, sizeof bytes, state);
}

static bool rebuild_queue(Player *player, const ResumeState *state)
{
    uint16_t track = 0;
    if (!play_queue_build(&player->queue, player->library, state->plan)) {
        return false;
    }
    play_queue_move_to(&player->queue, state->cursor);
    return play_queue_current(&player->queue, &track) &&
           (state->from_up_next || track == state->track);
}

static void load_up_next(Player *player)
{
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    if (read_legacy(PLAYER_UP_NEXT_PATH, bytes, sizeof bytes)) {
        player->up_next_revision++;
        if (!up_next_decode(bytes, sizeof bytes, player->fingerprint, &player->up_next)) {
            up_next_clear(&player->up_next);
        }
    }
}

static bool restore_session_queue(Player *player, const SessionState *session)
{
    const ResumeState *state = &session->resume;
    if (session->queue_kind == SESSION_QUEUE_EMPTY) {
        player->queue = (PlayQueue){.library = player->library};
        return true;
    }
    if (session->queue_kind == SESSION_QUEUE_GENERATED) {
        return rebuild_queue(player, state);
    }
    if (player_storage_order(&player->storage, &player->storage.order,
                             &session->order_generation) != STORE_READY ||
        !play_queue_use_order(&player->queue, player->library, &player->storage.order,
                              state->plan)) {
        return false;
    }
    play_queue_move_to(&player->queue, state->cursor);
    uint16_t track = 0;
    return play_queue_current(&player->queue, &track) && track == state->track;
}

static void restore_position(Player *player, const ResumeState *state)
{
    player->track = state->track;
    player->from_up_next = state->from_up_next;
    player->has_track = true;
    player->repeat = state->repeat;
    player->shuffle = state->plan.shuffle;
    player->state = PLAYER_PAUSED;
    player->paused_position_ms = state->position_ms;
    player->rewind_pause = RESUME_REWIND_COLD;
    note_spoken(player, player->track);
    player_change_volume(player, (int)state->volume - (int)player->volume);
}

bool player_restore(Player *player)
{
    SessionState session;
    StoreLoad result =
        player_storage_load(&player->storage, player->fingerprint,
                            library_count(player->library, LIBRARY_TRACKS), &session);
    if (result == STORE_READY) {
        for (int attempt = 0; attempt < 2; attempt++) {
            if (restore_session_queue(player, &session)) {
                player->up_next = session.up_next;
                player->up_next_revision++;
                restore_position(player, &session.resume);
                return true;
            }
            if (session_journal_previous(&player->storage.journal, &session) != STORE_READY) {
                break;
            }
        }
        return false;
    }
    if (result != STORE_EMPTY && player->storage.session_file.size != 0) {
        return false;
    }
    ResumeState state;
    load_up_next(player);
    if (!read_resume_state(&state) || state.library_fingerprint != player->fingerprint ||
        !rebuild_queue(player, &state)) {
        return false;
    }
    restore_position(player, &state);
    return true;
}

static HalAudioFormat current_format(const Player *player)
{
    HalAudioFormat unknown = {.codec = HAL_AUDIO_CODEC_UNKNOWN};
    if (!player->loaded) {
        return unknown;
    }
    HalAudioStatus status = hal_audio_status();
    return status.token == player->current_token ? status.format : unknown;
}

PlayerStatus player_status(const Player *player)
{
    LibraryTrack record;
    bool known = player->has_track && library_read_track(player->library, player->track, &record);
    return (PlayerStatus){
        .state = player->state,
        .has_track = player->has_track,
        .track = player->track,
        .position_ms = current_position(player),
        .duration_ms = known ? record.duration_ms : 0,
        .volume = player->volume,
        .shuffle = player->shuffle,
        .repeat = player->repeat,
        .place = player->has_track ? play_queue_place(&player->queue) : (PlayQueuePlace){0, 0},
        .format = current_format(player),
        .spoken = player->has_track && player->spoken,
    };
}

void player_refusal_text(HalAudioRefusal refusal, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    switch (refusal.problem) {
    case HAL_AUDIO_PROBLEM_RATE: {
        char rate[16];
        format_sample_rate(refusal.sample_rate, rate, sizeof rate);
        i18n_format_text(&builder, STR_RATE_UNSUPPORTED, rate);
        return;
    }
    case HAL_AUDIO_PROBLEM_FORMAT:
        string_builder_append(&builder, i18n_text(STR_TYPE_UNSUPPORTED));
        return;
    case HAL_AUDIO_PROBLEM_APE:
        string_builder_append(&builder, i18n_text(STR_APE_UNSUPPORTED));
        return;
    case HAL_AUDIO_PROBLEM_AAC_ONLY:
        string_builder_append(&builder, i18n_text(STR_M4A_UNSUPPORTED));
        return;
    case HAL_AUDIO_PROBLEM_UNREADABLE:
    case HAL_AUDIO_PROBLEM_NONE:
        break;
    }
    string_builder_append(&builder, i18n_text(STR_TRACK_UNREADABLE));
}

void player_set_replay_gain(Player *player, ReplayGainMode mode, bool prevent_clipping)
{
    mode = (uint32_t)mode < REPLAY_GAIN_MODE_COUNT ? mode : REPLAY_GAIN_OFF;
    if (player->replay_gain_mode == (uint8_t)mode && player->prevent_clipping == prevent_clipping) {
        return;
    }
    player->replay_gain_mode = (uint8_t)mode;
    player->prevent_clipping = prevent_clipping;
    if (!player->loaded) {
        return;
    }
    hal_audio_set_replay_gain(
        player->current_token,
        library_replay_gain_for(player->library, player->track, mode, prevent_clipping));
    if (player->has_next) {
        hal_audio_set_replay_gain(
            player->next_token,
            library_replay_gain_for(player->library, player->next_track, mode, prevent_clipping));
    }
}

void player_set_transitions(Player *player, bool fade, uint8_t tenths)
{
    if (player->fade_transitions == fade && player->transition_tenths == tenths) {
        return;
    }
    player->fade_transitions = fade;
    player->transition_tenths = tenths;
    player_requeue(player);
}
