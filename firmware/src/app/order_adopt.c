#include "player_private.h"

#include <stddef.h>

#include "library/spoken_path.h"
#include "playback/mix_identity.h"

bool player_use_order(Player *player, const TrackOrder *order, uint16_t position)
{
    CassetteState cassette = {0};
    return player_use_tape(player, order, position, &cassette);
}

bool player_use_tape(Player *player, const TrackOrder *order, uint16_t position,
                     const CassetteState *cassette)
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
    state.cassette = *cassette;
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
    player->cassette = *cassette;
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
