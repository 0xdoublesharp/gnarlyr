#include "player.h"
#include "player_private.h"

#include "playback/resume_rewind.h"

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

static void restore_position(Player *player, const ResumeState *state, bool cold_start)
{
    player->track = state->track;
    player->from_up_next = state->from_up_next;
    player->has_track = true;
    player->repeat = state->repeat;
    player->state = PLAYER_PAUSED;
    player->paused_position_ms = state->position_ms;
    player->rewind_pause = cold_start ? RESUME_REWIND_COLD : RESUME_REWIND_NONE;
    player_note_spoken(player, player->track);
    if (!player->spoken) {
        player->shuffle = state->plan.shuffle;
    }
    player_set_volume(player, PLAYER_OUTPUT_WIRED, state->volume);
}

bool player_restore(Player *player, bool cold_start)
{
    SessionState session;
    StoreLoad result =
        player_storage_load(&player->storage, player->fingerprint,
                            library_count(player->library, LIBRARY_TRACKS), &session);
    if (result == STORE_READY) {
        for (int attempt = 0; attempt < 2; attempt++) {
            if (restore_session_queue(player, &session)) {
                if (session.cassette.active && session.cassette.count != player->queue.count) {
                    return false;
                }
                player->up_next = session.up_next;
                player->up_next_revision++;
                restore_position(player, &session.resume, cold_start);
                player->cassette = session.cassette;
                if (player->cassette.active) {
                    player->rewind_pause = RESUME_REWIND_NONE;
                }
                return true;
            }
            if (session_journal_previous(&player->storage.journal, &session) != STORE_READY) {
                break;
            }
        }
    }
    return false;
}
