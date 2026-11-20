#include "player.h"
#include "player_private.h"

bool player_jump_to_up_next(Player *player, uint16_t index)
{
    player_queue_cancel(player);
    if (index >= player->up_next.count) {
        return false;
    }
    up_next_drop(&player->up_next, index);
    player->hold_pause = player->state == PLAYER_PAUSED;
    bool started = player_start_up_next(player);
    player->hold_pause = false;
    player_save(player);
    return started;
}

bool player_jump_to_queue(Player *player, PlayQueueCursor cursor)
{
    if (!cassette_contains(&player->cassette, cursor.position)) {
        return false;
    }
    player->cassette.flip = false;
    player_queue_cancel(player);
    up_next_clear(&player->up_next);
    player_save_up_next(player);
    player->from_up_next = false;
    play_queue_move_to(&player->queue, cursor);
    player->hold_pause = player->state == PLAYER_PAUSED;
    bool started = player_start_playable(player, 0, 1, player->repeat == REPEAT_ALL);
    player->hold_pause = false;
    player_save(player);
    return started;
}
