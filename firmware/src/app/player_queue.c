#include "player_private.h"

#include <stddef.h>

bool player_queue_busy(const Player *player)
{
    return player->editing.stage != 0;
}

static void finish(Player *player, PlayerQueueEditResult result)
{
    PlayerQueueJob *job = &player->editing;
    track_order_job_abort(&job->writer);
    job->stage = 0;
    job->result = result;
    job->serial++;
    if (job->resume) {
        hal_audio_resume();
    }
    job->resume = false;
}

void player_queue_cancel(Player *player)
{
    if (player_queue_busy(player)) {
        finish(player, PLAYER_QUEUE_EDIT_CANCELLED);
    }
}

static PlayerQueueEditResult begin(Player *player)
{
    if (player->cassette.active || player_queue_busy(player) || !player->has_track ||
        !player_queue_is_current(player)) {
        return PLAYER_QUEUE_EDIT_STALE;
    }
    bool resume = player->loaded && player->state == PLAYER_PLAYING;
    if (resume) {
        hal_audio_pause();
    }
    if (!player_queue_is_current(player) || !player_save(player)) {
        if (resume) {
            hal_audio_resume();
        }
        return PLAYER_QUEUE_EDIT_SAVE_FAILED;
    }
    uint32_t serial = player->editing.serial;
    player->editing = (PlayerQueueJob){.serial = serial,
                                       .resume = resume,
                                       .result = PLAYER_QUEUE_EDIT_WORKING,
                                       .stage = 1,
                                       .session = player_session_state(player, &player->up_next)};
    play_queue_stream_init(&player->editing.stream, &player->queue);
    return PLAYER_QUEUE_EDIT_WORKING;
}

PlayerQueueEditResult player_snapshot_queue(Player *player)
{
    PlayerQueueEditResult result = begin(player);
    if (result == PLAYER_QUEUE_EDIT_WORKING) {
        player->editing.snapshot = true;
    }
    return result;
}

PlayerQueueEditResult player_edit_queue(Player *player, uint32_t revision, uint16_t row,
                                        UpNextEdit edit)
{
    if (revision != player->up_next_revision) {
        return PLAYER_QUEUE_EDIT_STALE;
    }
    if ((unsigned)edit >= UP_NEXT_EDIT_COUNT || (row == 0 && edit != UP_NEXT_CLEAR)) {
        return PLAYER_QUEUE_EDIT_INVALID;
    }
    PlayerQueueEditResult result = begin(player);
    if (result == PLAYER_QUEUE_EDIT_WORKING) {
        player->editing.action = edit;
        player->editing.row = row;
    }
    return result;
}

PlayerUpNextResult player_queue_insert(Player *player, uint16_t track, bool next)
{
    if (track >= library_count(player->library, LIBRARY_TRACKS)) {
        return PLAYER_UP_NEXT_FAILED;
    }
    if (player->queue.count == TRACK_ORDER_CAPACITY) {
        return PLAYER_UP_NEXT_FULL;
    }
    PlayerQueueEditResult result = begin(player);
    if (result != PLAYER_QUEUE_EDIT_WORKING) {
        return result == PLAYER_QUEUE_EDIT_STALE ? PLAYER_UP_NEXT_CHANGED
                                                 : PLAYER_UP_NEXT_SAVE_FAILED;
    }
    player->editing.insert = true;
    player->editing.next = next;
    player->editing.edit.track = track;
    return PLAYER_UP_NEXT_WORKING;
}

static bool merged_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    PlayerQueueJob *job = context;
    if ((uint32_t)first + count > job->count) {
        return false;
    }
    TrackOrderSource base = play_queue_stream_source(&job->stream);
    for (uint16_t index = 0; index < count;) {
        uint32_t at = (uint32_t)first + index;
        if (at == job->current) {
            tracks[index++] = job->session.resume.track;
        } else if (at > job->current && at <= (uint32_t)job->current + job->session.up_next.count) {
            tracks[index++] = job->session.up_next.tracks[at - job->current - 1];
        } else {
            uint16_t run = (uint16_t)(count - index);
            if (at < job->current && run > job->current - at) {
                run = (uint16_t)(job->current - at);
            }
            uint32_t source = at < job->current ? at : at - job->session.up_next.count;
            source -= at > job->current && job->session.resume.from_up_next ? 1u : 0u;
            if (!base.read(base.context, (uint16_t)source, run, tracks + index)) {
                return false;
            }
            index = (uint16_t)(index + run);
        }
    }
    return true;
}

static uint16_t row_position(const PlayerQueueJob *job, uint16_t row)
{
    return (uint16_t)(((uint32_t)job->current + row) % job->count);
}

static bool set_edit(PlayerQueueJob *job)
{
    uint32_t available = job->session.resume.repeat == REPEAT_ALL
                             ? (uint32_t)job->count - 1
                             : (uint32_t)job->count - job->current - 1;
    if (job->row == 0 || job->row > available) {
        return false;
    }
    uint16_t from = row_position(job, job->row);
    job->edit.from = from;
    if (job->action == UP_NEXT_REMOVE) {
        job->edit.operation = TRACK_ORDER_REMOVE;
        job->current = (uint16_t)(job->current - (from < job->current ? 1u : 0u));
        return true;
    }
    uint16_t target;
    if (job->action == UP_NEXT_MOVE_LATER) {
        if (job->row == available) {
            return false;
        }
        target = row_position(job, (uint16_t)(job->row + 1));
        target = (uint16_t)(target + (from > target ? 1u : 0u));
    } else {
        if (job->row == 1) {
            return false;
        }
        uint16_t row = job->action == UP_NEXT_MOVE_FIRST ? 1 : (uint16_t)(job->row - 1);
        target = row_position(job, row);
        target = (uint16_t)(target - (from < target ? 1u : 0u));
    }
    job->edit.operation = TRACK_ORDER_MOVE;
    job->edit.to = target;
    job->current = (uint16_t)(job->current - (from < job->current ? 1u : 0u));
    job->current = (uint16_t)(job->current + (target <= job->current ? 1u : 0u));
    return true;
}

static bool prepare(Player *player)
{
    PlayerQueueJob *job = &player->editing;
    uint32_t count = job->stream.count + (uint32_t)job->session.up_next.count;
    bool extra = job->session.resume.from_up_next || job->stream.count == 0;
    count += extra ? 1u : 0u;
    job->current = job->stream.count == 0 ? 0 : job->stream.current;
    job->current = (uint16_t)(job->current + (extra && job->stream.count != 0 ? 1u : 0u));
    bool clear = !job->insert && !job->snapshot && job->action == UP_NEXT_CLEAR;
    if (clear) {
        job->current = 0;
        job->session.up_next.count = 0;
        count = 1;
    }
    if (count == 0 || count > TRACK_ORDER_CAPACITY) {
        return false;
    }
    job->count = (uint16_t)count;
    TrackOrderSource source = {merged_read, job, job->count};
    job->edit.operation = TRACK_ORDER_COPY;
    uint16_t original_current = job->current;
    if (job->insert) {
        job->edit.operation = TRACK_ORDER_INSERT;
        job->edit.from = job->next ? (uint16_t)(job->current + 1) : job->count;
    } else if (!job->snapshot && !clear && !set_edit(job)) {
        return false;
    }
    uint16_t target_current = job->current;
    job->current = original_current;
    job->session.resume.cursor = (PlayQueueCursor){.position = target_current};
    StoreLoad loaded;
    if (player->queue.stored_order != NULL) {
        job->pending = player->storage.order;
        loaded = STORE_READY;
    } else {
        loaded = player_storage_order(&player->storage, &job->pending, NULL);
    }
    return (loaded == STORE_READY || loaded == STORE_EMPTY) &&
           track_order_job_begin(&job->writer, &job->pending, source, job->edit);
}

static bool publish(Player *player)
{
    PlayerQueueJob *job = &player->editing;
    SessionState *state = &job->session;
    state->queue_kind = SESSION_QUEUE_STORED;
    state->order_generation = job->pending.generation;
    state->resume.from_up_next = false;
    up_next_clear(&state->up_next);
    uint16_t current = 0;
    if (!track_order_read(&job->pending, state->resume.cursor.position, 1, &current) ||
        current != state->resume.track || !player_store_session(player, state)) {
        return false;
    }
    player->storage.order = job->pending;
    play_queue_use_order(&player->queue, player->library, &player->storage.order,
                         state->resume.plan);
    play_queue_move_to(&player->queue, state->resume.cursor);
    up_next_clear(&player->up_next);
    player->from_up_next = false;
    player->up_next_revision++;
    player_requeue(player);
    return true;
}

void player_queue_step(Player *player)
{
    PlayerQueueJob *job = &player->editing;
    if (!player_queue_is_current(player)) {
        finish(player, PLAYER_QUEUE_EDIT_STALE);
    } else if (job->stage == 1) {
        StoreResult result = play_queue_stream_prepare(&job->stream);
        if (result == STORE_DONE) {
            if (prepare(player)) {
                job->stage = 2;
            } else {
                finish(player, PLAYER_QUEUE_EDIT_INVALID);
            }
        } else if (result == STORE_FAILED) {
            finish(player, PLAYER_QUEUE_EDIT_INVALID);
        }
    } else {
        StoreResult result = track_order_job_step(&job->writer);
        if (result == STORE_DONE) {
            finish(player, publish(player) ? PLAYER_QUEUE_EDITED : PLAYER_QUEUE_EDIT_SAVE_FAILED);
        } else if (result != STORE_WORKING) {
            finish(player, PLAYER_QUEUE_EDIT_SAVE_FAILED);
        }
    }
}
