#include "listening_store.h"

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
