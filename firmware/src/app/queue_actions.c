#include "player_private.h"

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
    return player_commit_up_next(player, &candidate);
}

PlayerQueueEditResult player_snapshot_queue(Player *player)
{
    PlayerQueueEditResult result = player_queue_begin(player);
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
    PlayerQueueEditResult result = player_queue_begin(player);
    if (result == PLAYER_QUEUE_EDIT_WORKING) {
        player->editing.action = edit;
        player->editing.row = row;
    }
    return result;
}
