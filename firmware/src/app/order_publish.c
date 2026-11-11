#include "order_publish.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/clock.h"
#include "player_private.h"

enum {
    STEPS_PER_CALL = 8,
    BUDGET_US = 2000
};

static bool reopen_fresh_session(Player *player)
{
    SessionJournal previous = player->storage.journal;
    if (!previous.needs_reload) {
        return true;
    }
    if (player->has_track || player->queue.count != 0 || player->queue.stored_order != NULL ||
        previous.valid || previous.config.file.size != 0) {
        return false;
    }
    SessionState state;
    StoreLoad load = player_storage_load(&player->storage, player->fingerprint,
                                         library_count(player->library, LIBRARY_TRACKS), &state);
    if (load == STORE_EMPTY && !player->storage.journal.needs_reload) {
        return true;
    }
    player->storage.journal = previous;
    return false;
}

static bool prepare(OrderPublish *job, TrackOrderSource source)
{
    Player *player = job->player;
    StoreLoad load = STORE_READY;
    if (player->queue.stored_order != NULL) {
        job->pending = player->storage.order;
    } else {
        load = player_storage_order(&player->storage, &job->pending, NULL);
    }
    return (load == STORE_EMPTY || load == STORE_READY) &&
           track_order_job_begin(&job->writer, &job->pending, source,
                                 (TrackOrderEdit){.operation = TRACK_ORDER_COPY});
}

bool order_publish_begin(OrderPublish *job, Player *player, TrackOrderSource source,
                         uint16_t position)
{
    *job = (OrderPublish){.player = player, .position = position};
    if (player == NULL || source.read == NULL || position >= source.count ||
        !reopen_fresh_session(player)) {
        return false;
    }
    player_queue_cancel(player);
    if (player->has_track && !player_save(player)) {
        return false;
    }
    job->active = prepare(job, source);
    return job->active;
}

static StoreResult finish(OrderPublish *job, bool success)
{
    track_order_job_abort(&job->writer);
    job->active = false;
    if (!success) {
        return STORE_FAILED;
    }
    player_toggle_pause(job->player);
    return STORE_DONE;
}

StoreResult order_publish_step(OrderPublish *job)
{
    if (!job->active) {
        return STORE_FAILED;
    }
    uint32_t started = hal_clock_microseconds();
    for (int step = 0; step < STEPS_PER_CALL; step++) {
        StoreResult result = track_order_job_step(&job->writer);
        if (result == STORE_DONE) {
            return finish(job, player_use_order(job->player, &job->pending, job->position));
        }
        if (result != STORE_WORKING) {
            return finish(job, false);
        }
        hal_audio_service();
        if (hal_clock_microseconds() - started >= BUDGET_US) {
            break;
        }
    }
    return STORE_WORKING;
}

void order_publish_cancel(OrderPublish *job)
{
    if (job->active) {
        track_order_job_abort(&job->writer);
        job->active = false;
    }
}
