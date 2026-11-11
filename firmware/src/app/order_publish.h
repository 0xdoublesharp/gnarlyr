#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "player.h"

typedef struct OrderPublish {
    Player *player;
    TrackOrder pending;
    TrackOrderJob writer;
    uint16_t position;
    bool active;
} OrderPublish;

bool order_publish_begin(OrderPublish *job, Player *player, TrackOrderSource source,
                         uint16_t position);
StoreResult order_publish_step(OrderPublish *job);
void order_publish_cancel(OrderPublish *job);
