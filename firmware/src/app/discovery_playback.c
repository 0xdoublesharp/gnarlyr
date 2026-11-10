#include "discovery_playback.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/clock.h"
#include "player_private.h"

typedef struct DiscoveryPlayback {
    DiscoveryScreen *owner;
    TrackOrder pending;
    TrackOrderJob writer;
    bool active;
} DiscoveryPlayback;

static DiscoveryPlayback work;

static void finish(DiscoveryScreen *screen, bool success)
{
    track_order_job_abort(&work.writer);
    work.active = false;
    screen->auto_play = false;
    discovery_screen_playback_done(screen, success);
    if (success) {
        player_toggle_pause(screen->config.player);
        screen->destination = screen->config.now_playing;
    }
}

void discovery_playback_cancel(DiscoveryScreen *screen)
{
    if (work.owner == screen && work.active) {
        track_order_job_abort(&work.writer);
        work.active = false;
    }
    screen->auto_play = false;
    if (screen->request != DISCOVERY_REQUEST_NONE) {
        screen->request = DISCOVERY_REQUEST_CANCEL_PLAY;
        discovery_screen_playback_done(screen, false);
    }
}

static bool reopen_fresh_session(Player *player)
{
    SessionJournal previous = player->storage.journal;
    if (!previous.needs_reload) {
        return true;
    }
    if (player->has_track || player->queue.count != 0 || player->queue.stored_order != NULL ||
        previous.valid || previous.config.file.size != 0) {
        return false;
    }
    SessionState state;
    StoreLoad load = player_storage_load(&player->storage, player->fingerprint,
                                         library_count(player->library, LIBRARY_TRACKS), &state);
    if (load == STORE_EMPTY && !player->storage.journal.needs_reload) {
        return true;
    }
    player->storage.journal = previous;
    return false;
}

static bool begin(DiscoveryScreen *screen)
{
    Player *player = screen->config.player;
    TrackOrderSource source = discovery_screen_order(screen);
    if (player == NULL || source.read == NULL || screen->play_position >= source.count ||
        !reopen_fresh_session(player)) {
        return false;
    }
    player_queue_cancel(player);
    if (player->has_track && !player_save(player)) {
        return false;
    }
    work = (DiscoveryPlayback){.owner = screen};
    StoreLoad load = STORE_READY;
    if (player->queue.stored_order != NULL) {
        work.pending = player->storage.order;
    } else {
        load = player_storage_order(&player->storage, &work.pending, NULL);
    }
    work.active = (load == STORE_EMPTY || load == STORE_READY) &&
                  track_order_job_begin(&work.writer, &work.pending, source,
                                        (TrackOrderEdit){.operation = TRACK_ORDER_COPY});
    return work.active;
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
    if (screen->request == DISCOVERY_REQUEST_CANCEL_PLAY ||
        (work.owner == screen && work.active && screen->request != DISCOVERY_REQUEST_PLAY)) {
        discovery_playback_cancel(screen);
        return;
    }
    if (screen->request != DISCOVERY_REQUEST_PLAY) {
        return;
    }
    if ((!work.active || work.owner != screen) && !begin(screen)) {
        discovery_screen_playback_done(screen, false);
        return;
    }
    uint32_t started = hal_clock_microseconds();
    for (int step = 0; step < 8; step++) {
        StoreResult result = track_order_job_step(&work.writer);
        if (result == STORE_DONE) {
            finish(screen,
                   player_use_order(screen->config.player, &work.pending, screen->play_position));
            return;
        }
        if (result != STORE_WORKING) {
            finish(screen, false);
            return;
        }
        hal_audio_service();
        if (hal_clock_microseconds() - started >= 2000) {
            return;
        }
    }
}
