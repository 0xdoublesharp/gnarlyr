#include "listening_store.h"

ListeningTotals listening_store_totals(const ListeningStore *store)
{
    ListeningTotals totals = store->log.root.counters.totals;
    totals.listened_ms += store->following ? store->active.listened_ms : 0;
    totals.listened_ms += store->waiting ? store->pending.listened_ms : 0;
    return totals;
}

static bool holds_key(const Library *library, uint32_t index, uint32_t key)
{
    ListeningTrack current;
    return listening_track_read(library, (uint16_t)index, &current) == STORE_READY &&
           current.key == key;
}

static bool track_with_key(const Library *library, const ListeningTrack *track, uint32_t *index)
{
    if (holds_key(library, track->index, track->key)) {
        *index = track->index;
        return true;
    }
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    for (*index = 0; *index < count; (*index)++) {
        if (holds_key(library, *index, track->key)) {
            return true;
        }
    }
    return false;
}

bool listening_store_text(void *context, const ListeningTrack *track, ListeningText *text)
{
    const ListeningStore *store = context;
    LibraryTrack record;
    LibraryAlbum album;
    LibraryGroup artist;
    uint32_t index = 0;
    *text = (ListeningText){0};
    if (!track_with_key(store->library, track, &index) ||
        !library_read_track(store->library, (uint16_t)index, &record)) {
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
