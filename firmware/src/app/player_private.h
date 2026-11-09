#pragma once

#include "player.h"

SessionState player_session_state(const Player *player, const UpNext *up_next);
bool player_store_session(Player *player, const SessionState *state);
void player_requeue(Player *player);
void player_queue_step(Player *player);
PlayerUpNextResult player_queue_insert(Player *player, uint16_t track, bool next);
bool player_queue_is_current(const Player *player);
