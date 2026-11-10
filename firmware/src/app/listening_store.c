#include "listening_store.h"

#include <stddef.h>

#include "hal/storage.h"
#include "hal/system.h"

enum {
    RETRY_MS = 1000,
    FINISH_TOLERANCE_MS = 2000,
    SECOND_MS = 1000
};

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const ListeningFile *source = context;
    HalFile file;
    if (!hal_file_open(&file, source->path)) {
        return false;
    }
    bool read = offset <= file.size && length <= file.size - offset &&
                hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    hal_system_keep_alive();
    return read;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    const ListeningFile *file = context;
    bool written = hal_file_write_at(file->path, offset, bytes, length);
    hal_system_keep_alive();
    return written;
}

StoreFile listening_store_file(ListeningFile *file, const char *path, uint32_t bytes)
{
    file->path = path;
    return (StoreFile){.source = {read_file, file}, .write = write_file, .size = bytes};
}

void listening_store_attach(ListeningStore *store, const Library *library, StoreFile file)
{
    *store = (ListeningStore){.library = library};
    StoreLoad load = listening_log_open(&store->log, (ListeningLogConfig){file, true});
    store->failed = store_blocking(load);
}

void listening_store_load(ListeningStore *store, const Library *library)
{
    *store = (ListeningStore){.library = library};
    StoreFile file =
        listening_store_file(&store->file, LISTENING_LOG_PATH, LISTENING_LOG_FILE_BYTES);
    StoreLoad load = listening_log_open(&store->log, (ListeningLogConfig){file, true});
    store->failed = store_blocking(load);
}

static bool reload_pending(ListeningStore *store)
{
    ListeningTotals session = store->log.session;
    ListeningLogConfig config = {store->log.file, store->log.enabled};
    StoreLoad load = listening_log_open(&store->log, config);
    store->log.session = session;
    if (store_blocking(load)) {
        return false;
    }
    if (store->log.root.next == store->pending_sequence + 1) {
        listening_totals_add(&store->log.session, &store->pending);
        store->waiting = false;
    }
    return store->log.root.next == store->pending_sequence || !store->waiting;
}

static void save_pending(ListeningStore *store, uint32_t now_ms)
{
    if (!store->waiting || (int32_t)(now_ms - store->retry_ms) < 0) {
        return;
    }
    store->retry_ms = now_ms + RETRY_MS;
    if (store->log.needs_reload && !reload_pending(store)) {
        store->failed = true;
        return;
    }
    if (!store->waiting) {
        store->failed = false;
        return;
    }
    StoreResult result = listening_log_append(&store->log, &store->pending);
    store->waiting = result != STORE_DONE && result != STORE_IDLE;
    store->failed = store->waiting;
}

static void finish(ListeningStore *store, ListeningEnd end, uint32_t now_ms)
{
    if (!store->following) {
        return;
    }
    store->following = false;
    if (store->active.listened_ms == 0) {
        return;
    }
    if (store->waiting) {
        store->failed = true;
        return;
    }
    store->pending = store->active;
    store->pending.end = end;
    store->pending_sequence = store->log.root.next;
    store->waiting = true;
    store->retry_ms = now_ms;
    save_pending(store, now_ms);
}

static bool lossless(HalAudioCodec codec)
{
    return codec == HAL_AUDIO_CODEC_FLAC || codec == HAL_AUDIO_CODEC_WAV ||
           codec == HAL_AUDIO_CODEC_ALAC;
}

static void begin(ListeningStore *store, ListeningObservation observation, uint32_t now_ms)
{
    const PlayerStatus *status = &observation.status;
    ListeningTrack track;
    if (!status->has_track || store->library == NULL ||
        listening_track_read(store->library, status->track, &track) != STORE_READY) {
        return;
    }
    store->active = (ListeningPlay){
        .track = track,
        .length_ms = status->duration_ms,
        .start_ms = status->position_ms,
        .end_ms = status->position_ms,
        .uptime_s = now_ms / SECOND_MS,
        .output = observation.output,
        .format = listening_format_class(lossless(status->format.codec), status->format.sample_rate,
                                         status->format.bits_per_sample),
    };
    store->following = true;
}

static bool same_play(ListeningObservation first, ListeningObservation second)
{
    return first.status.has_track && second.status.has_track && first.token == second.token &&
           first.status.track == second.status.track;
}

static uint32_t elapsed_audio(const ListeningStore *store, ListeningObservation observation,
                              uint32_t now_ms)
{
    if (store->previous.status.state != PLAYER_PLAYING) {
        return 0;
    }
    uint32_t elapsed = now_ms - store->last_ms;
    uint32_t before = store->previous.status.position_ms;
    uint32_t after = observation.status.position_ms;
    if (same_play(store->previous, observation)) {
        uint32_t moved = after >= before ? after - before : elapsed;
        return moved < elapsed ? moved : elapsed;
    }
    uint32_t length = store->previous.status.duration_ms;
    uint32_t left = length > before ? length - before : 0;
    return left != 0 && left < elapsed ? left : elapsed;
}

static ListeningEnd changed_end(const ListeningStore *store)
{
    uint32_t duration = store->active.length_ms;
    uint32_t end = store->active.end_ms;
    return duration != 0 && (end >= duration || duration - end <= FINISH_TOLERANCE_MS)
               ? LISTENING_FINISHED
               : LISTENING_SKIPPED;
}

void listening_store_follow(ListeningStore *store, ListeningObservation observation,
                            uint32_t now_ms)
{
    save_pending(store, now_ms);
    bool same = store->observed && same_play(store->previous, observation);
    if (store->following) {
        if (same && observation.status.duration_ms != 0) {
            store->active.length_ms = observation.status.duration_ms;
        }
        if (same && observation.status.format.codec != HAL_AUDIO_CODEC_UNKNOWN) {
            HalAudioFormat format = observation.status.format;
            store->active.format = listening_format_class(
                lossless(format.codec), format.sample_rate, format.bits_per_sample);
        }
        uint32_t elapsed = elapsed_audio(store, observation, now_ms);
        uint32_t room = UINT32_MAX - store->active.listened_ms;
        store->active.listened_ms += elapsed < room ? elapsed : room;
        store->active.end_ms =
            same ? observation.status.position_ms : store->previous.status.position_ms + elapsed;
        if (!same) {
            ListeningEnd end = changed_end(store);
            finish(store,
                   !observation.status.has_track && end == LISTENING_SKIPPED ? LISTENING_STOPPED
                                                                             : end,
                   now_ms);
        }
    }
    if (!store->following && observation.status.has_track) {
        begin(store, observation, now_ms);
    }
    store->previous = observation;
    store->last_ms = now_ms;
    store->observed = true;
}

void listening_store_flush(ListeningStore *store, ListeningObservation observation, uint32_t now_ms)
{
    listening_store_follow(store, observation, now_ms);
    finish(store, LISTENING_STOPPED, now_ms);
    save_pending(store, now_ms);
    store->observed = false;
}

ListeningTotals listening_store_totals(const ListeningStore *store)
{
    ListeningTotals totals = store->log.root.counters.totals;
    totals.listened_ms += store->following ? store->active.listened_ms : 0;
    totals.listened_ms += store->waiting ? store->pending.listened_ms : 0;
    return totals;
}

bool listening_store_text(void *context, const ListeningTrack *track, ListeningText *text)
{
    const ListeningStore *store = context;
    ListeningTrack current;
    LibraryTrack record;
    LibraryAlbum album;
    LibraryGroup artist;
    *text = (ListeningText){0};
    if (listening_track_read(store->library, track->index, &current) != STORE_READY ||
        current.key != track->key || !library_read_track(store->library, track->index, &record)) {
        return false;
    }
    bool read = library_read_string(store->library, record.title, text->title, sizeof text->title);
    if (library_read_album(store->library, record.album, &album)) {
        read &= library_read_string(store->library, album.title, text->album, sizeof text->album);
    }
    if (library_read_group(store->library, LIBRARY_ARTISTS, record.artist, &artist)) {
        read &= library_read_string(store->library, artist.name, text->artist, sizeof text->artist);
    }
    text->number = record.number;
    return read;
}
