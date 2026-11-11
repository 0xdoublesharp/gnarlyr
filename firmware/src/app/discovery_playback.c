#include "discovery_playback.h"

#include <stddef.h>

#include "order_publish.h"

typedef struct DiscoveryPlayback {
    DiscoveryScreen *owner;
    OrderPublish publish;
} DiscoveryPlayback;

static DiscoveryPlayback work;

static void finish(DiscoveryScreen *screen, bool success)
{
    screen->auto_play = false;
    discovery_screen_playback_done(screen, success);
    if (success) {
        screen->destination = screen->config.now_playing;
    }
}

void discovery_playback_cancel(DiscoveryScreen *screen)
{
    if (work.owner == screen) {
        order_publish_cancel(&work.publish);
    }
    screen->auto_play = false;
    if (screen->request != DISCOVERY_REQUEST_NONE) {
        screen->request = DISCOVERY_REQUEST_CANCEL_PLAY;
        discovery_screen_playback_done(screen, false);
    }
}

static bool begin(DiscoveryScreen *screen)
{
    work.owner = screen;
    return order_publish_begin(&work.publish, screen->config.player, discovery_screen_order(screen),
                               screen->play_position);
}

static void auto_play(DiscoveryScreen *screen)
{
    if (!screen->auto_play || screen->request != DISCOVERY_REQUEST_NONE) {
        return;
    }
    LibraryViewState state = discovery_screen_state(screen);
    if (state == LIBRARY_VIEW_READY) {
        TrackOrderSource source = discovery_screen_order(screen);
        screen->auto_play = false;
        if (source.count != 0) {
            screen->play_position = 0;
            screen->request = DISCOVERY_REQUEST_PLAY;
        }
    } else if (state != LIBRARY_VIEW_SCANNING && state != LIBRARY_VIEW_SORTING) {
        screen->auto_play = false;
    }
}

void discovery_playback_step(DiscoveryScreen *screen, uint32_t now_ms)
{
    (void)now_ms;
    auto_play(screen);
    bool active = work.owner == screen && work.publish.active;
    if (screen->request == DISCOVERY_REQUEST_CANCEL_PLAY ||
        (active && screen->request != DISCOVERY_REQUEST_PLAY)) {
        discovery_playback_cancel(screen);
        return;
    }
    if (screen->request != DISCOVERY_REQUEST_PLAY) {
        return;
    }
    if (!active && !begin(screen)) {
        discovery_screen_playback_done(screen, false);
        return;
    }
    StoreResult result = order_publish_step(&work.publish);
    if (result != STORE_WORKING) {
        finish(screen, result == STORE_DONE);
    }
}
