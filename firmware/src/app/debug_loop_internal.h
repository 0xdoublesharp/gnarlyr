#pragma once

#include <stdint.h>

#include "app/debug_loop.h"
#include "core/key_tracker.h"
#include "player.h"

#ifdef ECHO_DEBUG_LOOP

enum {
    DEBUG_LOOP_ABOUT_ROWS = 1
};

void debug_loop_key(KeyEvent event);
void debug_loop_screen(const char *title, int depth);
void debug_loop_follow(uint32_t now_ms, const Player *player);

#else

enum {
    DEBUG_LOOP_ABOUT_ROWS = 0
};

static inline void debug_loop_key(KeyEvent event)
{
    (void)event;
}

static inline void debug_loop_screen(const char *title, int depth)
{
    (void)title;
    (void)depth;
}

static inline void debug_loop_follow(uint32_t now_ms, const Player *player)
{
    (void)now_ms;
    (void)player;
}

#endif
