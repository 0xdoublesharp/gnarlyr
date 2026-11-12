#include "hidden_tracks.h"

#include "core/string_builder.h"
#include "hal/audio.h"
#include "hal/storage.h"
#include "music_check.h"
#include "player_private.h"

enum {
    SERVICE_EVERY = 1024,
    NO_POSITION = 0xFFFF
};

static uint32_t record_offset(uint16_t record)
{
    return HIDDEN_TRACKS_HEAD_BYTES + (uint32_t)record * HIDDEN_TRACKS_RECORD_BYTES;
}

static bool read_bytes(uint32_t offset, void *bytes, uint32_t length)
{
    HalFile file;
    if (!hal_file_open(&file, HIDDEN_TRACKS_PATH)) {
        return false;
    }
    bool read = hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return read;
}

uint16_t hidden_tracks_read_head(HiddenTracksHead *head)
{
    *head = (HiddenTracksHead){.magic = 0};
    bool known = read_bytes(0, head, sizeof *head) && head->magic == HIDDEN_TRACKS_MAGIC &&
                 head->records <= HIDDEN_TRACKS_RECORDS;
    head->records = known ? head->records : 0;
    return head->records;
}

bool hidden_tracks_read(uint16_t record, MixIdentity *identity)
{
    uint8_t bytes[HIDDEN_TRACKS_RECORD_BYTES];
    return read_bytes(record_offset(record), bytes, sizeof bytes) &&
           mix_identity_decode(bytes, sizeof bytes, identity) != 0;
}

bool hidden_tracks_write(uint16_t record, const MixIdentity *identity)
{
    uint8_t bytes[HIDDEN_TRACKS_RECORD_BYTES] = {0};
    return mix_identity_encode(identity, bytes) != 0 &&
           hal_file_write_at(HIDDEN_TRACKS_PATH, record_offset(record), bytes, sizeof bytes);
}

bool hidden_tracks_prepare(HiddenTracks tracks, HiddenTracksHead *head)
{
    LibraryStore *store = tracks.store;
    if (store == NULL || !store->available) {
        return false;
    }
    (void)hidden_tracks_read_head(head);
    head->magic = HIDDEN_TRACKS_MAGIC;
    head->fingerprint = library_fingerprint(&store->library);
    if (store->library.hidden == NULL) {
        store->hidden.count = 0;
    }
    HalReserveResult reserved = hal_file_reserve(HIDDEN_TRACKS_PATH, HIDDEN_TRACKS_FILE_BYTES);
    return reserved == HAL_RESERVE_EXISTS || reserved == HAL_RESERVE_CREATED;
}

bool hidden_tracks_save(HiddenTracks tracks, HiddenTracksHead *head)
{
    LibraryStore *store = tracks.store;
    head->set = store->hidden;
    store->library.hidden = &store->hidden;
    return hal_file_write_at(HIDDEN_TRACKS_PATH, 0, head, sizeof *head);
}

static uint16_t title_of(const Library *library, uint16_t track)
{
    uint32_t count = library_count(library, LIBRARY_TITLE_ORDER);
    for (uint32_t position = 0; position < count; position++) {
        uint16_t value = 0;
        if (position % SERVICE_EVERY == 0) {
            hal_audio_service();
        }
        if (library_read_index(library, LIBRARY_TITLE_ORDER, position, &value) && value == track) {
            return (uint16_t)position;
        }
    }
    return NO_POSITION;
}

static bool sheet_lists(const Library *library, uint16_t folder, uint16_t track)
{
    LibraryFolder record;
    if (!library_read_folder(library, folder, &record) ||
        (record.flags & LIBRARY_FOLDER_SHEET) == 0) {
        return false;
    }
    for (uint32_t link = 0; link < record.track_count; link++) {
        uint16_t value = 0;
        if (library_read_index(library, LIBRARY_LINKS,
                               record.first_link + record.subfolder_count + link, &value) &&
            value == track) {
            return true;
        }
    }
    return false;
}

static uint16_t listing_folder(const Library *library, uint16_t track)
{
    LibraryTrack record;
    LibraryFolder holder;
    if (!library_read_track(library, track, &record) ||
        !library_read_folder(library, record.folder, &holder)) {
        return LIBRARY_NONE;
    }
    for (uint32_t index = 0; index < holder.subfolder_count; index++) {
        uint16_t sheet = 0;
        if (library_read_index(library, LIBRARY_LINKS, holder.first_link + index, &sheet) &&
            sheet_lists(library, sheet, track)) {
            return sheet;
        }
    }
    return record.folder;
}

void hidden_tracks_exclude(HiddenTracks tracks, uint16_t track)
{
    const Library *library = &tracks.store->library;
    LibraryHidden *set = &tracks.store->hidden;
    if (library_hidden(library, track) || set->count >= LIBRARY_HIDDEN_CAPACITY) {
        return;
    }
    uint16_t title = title_of(library, track);
    uint16_t at = set->count;
    for (; at > 0 && set->titles[at - 1] > title; at--) {
        set->titles[at] = set->titles[at - 1];
    }
    set->titles[at] = title;
    set->tracks[set->count] = track;
    set->folders[set->count++] = listing_folder(library, track);
}

static void include_at(HiddenTracks tracks, uint16_t index)
{
    LibraryHidden *set = &tracks.store->hidden;
    uint16_t title = title_of(&tracks.store->library, set->tracks[index]);
    set->count--;
    set->tracks[index] = set->tracks[set->count];
    set->folders[index] = set->folders[set->count];
    bool found = false;
    for (uint16_t at = 0; at < set->count; at++) {
        found = found || set->titles[at] == title;
        if (found) {
            set->titles[at] = set->titles[at + 1];
        }
    }
}

void hidden_tracks_include(HiddenTracks tracks, uint16_t track)
{
    const LibraryHidden *set = &tracks.store->hidden;
    for (uint16_t index = 0; index < set->count; index++) {
        if (set->tracks[index] == track) {
            include_at(tracks, index);
            return;
        }
    }
}

uint16_t hidden_tracks_anchor(const Player *player)
{
    uint16_t anchor = player->track;
    (void)play_queue_track_at(&player->queue, player->queue.cursor, &anchor);
    return anchor;
}

void hidden_tracks_replan(Player *player, uint16_t anchor)
{
    player_queue_cancel(player);
    if (!player->has_track) {
        return;
    }
    if (player->queue.stored_order == NULL) {
        PlayQueuePlan plan = player->queue.plan;
        plan.anchor_track = anchor;
        (void)play_queue_build(&player->queue, player->library, plan);
    }
    player_requeue(player);
    (void)player_save(player);
}

static bool release(HiddenTracks tracks, const MixIdentity *identity)
{
    const LibraryHidden *set = &tracks.store->hidden;
    for (uint16_t index = 0; index < set->count; index++) {
        MixIdentity listed;
        if (mix_identity_read(&tracks.store->library, set->tracks[index], &listed) == STORE_READY &&
            mix_identity_equal(&listed, identity)) {
            include_at(tracks, index);
            return true;
        }
    }
    return false;
}

static bool forget_record(HiddenTracksHead *head, uint16_t record)
{
    for (uint16_t at = (uint16_t)(record + 1); at < head->records; at++) {
        MixIdentity identity;
        if (!hidden_tracks_read(at, &identity) ||
            !hidden_tracks_write((uint16_t)(at - 1), &identity)) {
            return false;
        }
    }
    head->records--;
    return true;
}

static void ask_for_rescan(void)
{
    static const uint8_t unknown[sizeof(uint32_t)] = {0};
    (void)hal_file_write_at(MUSIC_CHECK_PATH, 0, unknown, sizeof unknown);
}

HiddenResult hidden_tracks_unhide(HiddenTracks tracks, uint16_t record)
{
    HiddenTracksHead head;
    MixIdentity identity;
    if (!hidden_tracks_prepare(tracks, &head) || record >= head.records ||
        !hidden_tracks_read(record, &identity)) {
        return HIDDEN_FAILED;
    }
    uint16_t anchor = hidden_tracks_anchor(tracks.player);
    if (!forget_record(&head, record)) {
        return HIDDEN_FAILED;
    }
    bool shown = release(tracks, &identity);
    if (!hidden_tracks_save(tracks, &head)) {
        return HIDDEN_FAILED;
    }
    if (!shown) {
        ask_for_rescan();
        return HIDDEN_REFUSED;
    }
    hidden_tracks_replan(tracks.player, anchor);
    return HIDDEN_DONE;
}

void hidden_tracks_file_name(const char *path, char *name, size_t capacity)
{
    const char *start = path;
    for (const char *at = path; *at != 0; at++) {
        start = *at == '/' ? at + 1 : start;
    }
    StringBuilder builder = string_builder_make(name, capacity);
    string_builder_append(&builder, start);
}
