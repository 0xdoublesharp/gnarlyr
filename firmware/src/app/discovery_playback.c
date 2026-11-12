#include "discovery_playback.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/clock.h"
#include "library/album_order.h"
#include "order_publish.h"

typedef struct DiscoveryPlayback {
    DiscoveryScreen *owner;
    OrderPublish publish;
    AlbumOrder albums;
    bool counting;
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
    work.counting = false;
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

static bool prepare_albums(DiscoveryScreen *screen)
{
    if (!work.counting) {
        work.owner = screen;
        uint32_t seed = screen->shuffle_albums ? hal_clock_microseconds() | 1u : 0;
        if (!album_order_begin(&work.albums, screen->config.library, screen->query.source, seed)) {
            return false;
        }
        work.counting = true;
    }
    uint32_t started = hal_clock_microseconds();
    for (int step = 0; step < 8; step++) {
        if (!album_order_step(&work.albums)) {
            return false;
        }
        if (work.albums.ready) {
            TrackOrderSource source = {album_order_read, &work.albums, (uint16_t)work.albums.count};
            work.counting = false;
            screen->request = DISCOVERY_REQUEST_PLAY;
            return order_publish_begin(&work.publish, screen->config.player, source, 0);
        }
        hal_audio_service();
        if (hal_clock_microseconds() - started >= 2000) {
            break;
        }
    }
    return true;
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
    if (screen->request == DISCOVERY_REQUEST_ALBUMS) {
        if (!prepare_albums(screen)) {
            work.counting = false;
            finish(screen, false);
        }
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
