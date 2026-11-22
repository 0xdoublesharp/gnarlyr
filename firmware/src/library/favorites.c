#include "library/favorites.h"

#include "core/bytes.h"
#include "core/record.h"
#include "favorites_format.h"

static int find_key(const Favorites *favorites, uint32_t key)
{
    for (int index = 0; index < favorites->count; index++) {
        if (favorites->keys[index] == key) {
            return index;
        }
    }
    return -1;
}

static void remove_at(Favorites *favorites, int position)
{
    for (int index = position; index + 1 < favorites->count; index++) {
        favorites->keys[index] = favorites->keys[index + 1];
    }
    favorites->count--;
}

void favorites_clear(Favorites *favorites)
{
    favorites->count = 0;
}

bool favorites_contains(const Favorites *favorites, uint32_t key)
{
    return find_key(favorites, key) >= 0;
}

bool favorites_toggle(Favorites *favorites, uint32_t key)
{
    int position = find_key(favorites, key);
    if (position >= 0) {
        remove_at(favorites, position);
        return false;
    }
    if (favorites->count == FAVORITES_CAPACITY) {
        return false;
    }
    favorites->keys[favorites->count++] = key;
    return true;
}

uint32_t favorites_encode(const Favorites *favorites, uint8_t *bytes)
{
    record_begin(bytes, &KIND);
    bytes_put_le16(bytes + COUNT_FIELD, favorites->count);
    uint32_t offset = KEYS_FIELD;
    for (int index = 0; index < favorites->count; index++, offset += KEY_SIZE) {
        bytes_put_le32(bytes + offset, favorites->keys[index]);
    }
    record_seal(bytes, offset);
    return offset + CHECKSUM_SIZE;
}

static void pause_between_albums(FavoritesCollector collector, uint32_t album)
{
    if (collector.between_albums != NULL && album % FAVORITES_ALBUMS_PER_PAUSE == 0) {
        collector.between_albums(collector.context);
    }
}

uint16_t favorites_collect(const Favorites *favorites, const Library *library,
                           FavoritesCollector collector)
{
    uint16_t found = 0;
    uint32_t album_count = library_count(library, LIBRARY_ALBUMS);
    for (uint32_t album = 0; album < album_count && found < collector.capacity; album++) {
        pause_between_albums(collector, album);
        if (favorites_contains(favorites, library_album_key(library, album))) {
            collector.albums[found++] = (uint16_t)album;
        }
    }
    return found;
}
