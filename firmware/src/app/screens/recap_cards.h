#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/strings.h"
#include "screens/listening_screen.h"

enum {
    RECAP_FLOURISH_MS = 1000
};

void recap_cards_bind(CoverCache *covers, const Settings *settings);
void recap_cards_open(ListeningScreen *screen, uint32_t now_ms);
void recap_cards_update(ListeningScreen *screen, uint32_t now_ms);
void recap_cards_motion(ListeningScreen *screen, uint32_t now_ms);
bool recap_cards_turn(ListeningScreen *screen, int step);
uint8_t recap_cards_count(const ListeningScreen *screen);
StringId recap_cards_title(const ListeningScreen *screen);
void recap_cards_draw(ListeningScreen *screen, Canvas *canvas, Rect content);
