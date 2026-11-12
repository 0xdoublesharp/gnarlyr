#include "track_removal.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"

enum {
    ROOTED_PATH_BYTES = MIX_PATH_BYTES + 1
};

bool track_removal_path_allowed(const char *path)
{
    bool start = true;
    if (path == NULL || path[0] == 0) {
        return false;
    }
    for (const char *at = path; *at != 0; at++) {
        if (*at == '\\' || *at == ':' || (start && (*at == '.' || *at == '/'))) {
            return false;
        }
        start = *at == '/';
    }
    return !start;
}

static void leave_track(Player *player, uint16_t track, bool stop)
{
    PlayerStatus status = player_status(player);
    if (!status.has_track || status.track != track) {
        return;
    }
    player_skip(player, 1);
    if (stop && player_status(player).track == track) {
        player_release_output(player);
        hal_audio_release();
    }
}

static void drop_up_next(Player *player, uint16_t track)
{
    UpNext *up_next = &player->up_next;
    uint16_t kept = 0;
    for (uint16_t index = 0; index < up_next->count; index++) {
        if (up_next->tracks[index] != track) {
            up_next->tracks[kept++] = up_next->tracks[index];
        }
    }
    player->up_next_revision += kept != up_next->count ? 1u : 0u;
    up_next->count = kept;
}

static HiddenResult publish(HiddenTracks tracks, HiddenTracksHead *head, uint16_t track, bool stop)
{
    Player *player = tracks.player;
    if (!library_hidden(&tracks.store->library, track) &&
        tracks.store->hidden.count >= LIBRARY_HIDDEN_CAPACITY) {
        return HIDDEN_RESCAN_FIRST;
    }
    leave_track(player, track, stop);
    uint16_t anchor = hidden_tracks_anchor(player);
    hidden_tracks_exclude(tracks, track);
    if (!hidden_tracks_save(tracks, head)) {
        hidden_tracks_include(tracks, track);
        return HIDDEN_FAILED;
    }
    drop_up_next(player, track);
    hidden_tracks_replan(player, anchor);
    return HIDDEN_DONE;
}

HiddenResult track_removal_hide(HiddenTracks tracks, uint16_t track)
{
    HiddenTracksHead head;
    MixIdentity identity;
    if (mix_identity_read(&tracks.store->library, track, &identity) != STORE_READY ||
        !hidden_tracks_prepare(tracks, &head)) {
        return HIDDEN_FAILED;
    }
    if (head.records >= HIDDEN_TRACKS_RECORDS) {
        return HIDDEN_FULL;
    }
    if (!hidden_tracks_write(head.records, &identity)) {
        return HIDDEN_FAILED;
    }
    head.records++;
    return publish(tracks, &head, track, false);
}

static bool deletable(const MixIdentity *identity)
{
    return identity->start_ms == 0 && identity->end_ms == 0 &&
           track_removal_path_allowed(identity->path);
}

static HiddenResult read_deletable(HiddenTracks tracks, uint16_t track, MixIdentity *identity)
{
    if (mix_identity_read(&tracks.store->library, track, identity) != STORE_READY) {
        return HIDDEN_FAILED;
    }
    return deletable(identity) ? HIDDEN_DONE : HIDDEN_REFUSED;
}

HiddenResult track_removal_delete(HiddenTracks tracks, uint16_t track)
{
    HiddenTracksHead head;
    MixIdentity identity;
    char path[ROOTED_PATH_BYTES];
    HiddenResult result = read_deletable(tracks, track, &identity);
    if (result != HIDDEN_DONE) {
        return result;
    }
    if (!hidden_tracks_prepare(tracks, &head)) {
        return HIDDEN_FAILED;
    }
    StringBuilder builder = string_builder_make(path, sizeof path);
    string_builder_append_char(&builder, '/');
    string_builder_append(&builder, identity.path);
    result = publish(tracks, &head, track, true);
    if (result == HIDDEN_DONE && !hal_file_remove(path)) {
        uint16_t anchor = hidden_tracks_anchor(tracks.player);
        hidden_tracks_include(tracks, track);
        (void)hidden_tracks_save(tracks, &head);
        hidden_tracks_replan(tracks.player, anchor);
        result = HIDDEN_FAILED;
    }
    return result;
}

static void tell(const TrackRemoval *removal, StringId text)
{
    if (removal->toast != NULL) {
        toast_show(removal->toast, i18n_text(text), hal_clock_milliseconds());
    }
}

static const StringId HIDE_TEXT[] = {STR_TRACK_HIDDEN, STR_HIDDEN_FULL, STR_HIDE_FAILED,
                                     STR_HIDE_FAILED, STR_RESCAN_FIRST};
static const StringId DELETE_TEXT[] = {STR_FILE_DELETED, STR_HIDDEN_FULL, STR_DELETE_PART_REFUSED,
                                       STR_DELETE_FILE_FAILED};

static Screen *ask_delete(const TrackRemoval *removal, uint16_t track)
{
    SongActions *actions = removal->actions;
    MixIdentity identity;
    HiddenResult result = read_deletable(removal->tracks, track, &identity);
    if (result != HIDDEN_DONE) {
        tell(removal, DELETE_TEXT[result]);
        return NULL;
    }
    MenuConfig config = actions->menu.config;
    MenuItem choice = actions->items[config.item_count - 1];
    actions->items[0] = choice;
    actions->items[0].label = i18n_text(STR_CANCEL);
    actions->items[1] = choice;
    hidden_tracks_file_name(identity.path, actions->title, sizeof actions->title);
    actions->confirming = true;
    config.title = actions->title;
    config.item_count = 2;
    return menu_screen_init(&actions->menu, config);
}

Screen *track_removal_manage(TrackRemoval removal, uint16_t track, SongManage manage)
{
    switch (manage) {
    case SONG_MANAGE_HIDE:
        tell(&removal, HIDE_TEXT[track_removal_hide(removal.tracks, track)]);
        return NULL;
    case SONG_MANAGE_ASK:
        return ask_delete(&removal, track);
    case SONG_MANAGE_DELETE:
        tell(&removal, DELETE_TEXT[track_removal_delete(removal.tracks, track)]);
        return NULL;
    case SONG_MANAGE_KEEP:
    case SONG_MANAGE_TAPE:
        break;
    }
    return NULL;
}
