#include "playback/listening_track.h"

#include <stddef.h>

#include "core/hash.h"
#include "playback/mix_identity.h"

enum {
    NAME_BYTES = 256
};

static StoreLoad track_key(const Library *library, uint16_t track, uint32_t *key)
{
    MixIdentity identity;
    StoreLoad load = mix_identity_read(library, track, &identity);
    *key = load == STORE_READY ? mix_identity_hash(&identity) : 0;
    return load;
}

static uint32_t artist_key(const Library *library, uint16_t artist)
{
    LibraryGroup group;
    char name[NAME_BYTES];
    size_t length = 0;
    if (!library_read_group(library, LIBRARY_ARTISTS, artist, &group) ||
        !library_read_string(library, group.name, name, sizeof name)) {
        return 0;
    }
    while (name[length] != '\0') {
        length++;
    }
    return hash_update(HASH_START, name, length);
}

StoreLoad listening_track_read(const Library *library, uint16_t track, ListeningTrack *identity)
{
    LibraryTrack record;
    *identity = (ListeningTrack){.index = track};
    StoreLoad load = track_key(library, track, &identity->key);
    if (load != STORE_READY) {
        return load;
    }
    if (!library_read_track(library, track, &record)) {
        return STORE_UNREADABLE;
    }
    identity->album = library_album_key(library, record.album);
    identity->artist = artist_key(library, record.artist);
    return STORE_READY;
}
