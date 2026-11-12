#include "library/album_order.h"

static bool read_album(const AlbumOrder *order, uint32_t position, LibraryAlbum *album)
{
    uint16_t id;
    uint32_t link = order->shuffled ? permutation_at(&order->permutation, position) : position;
    return position < order->group.link_count &&
           library_read_index(order->library, LIBRARY_LINKS, order->group.first_link + link, &id) &&
           library_read_album(order->library, id, album) &&
           (uint32_t)album->first_track + album->track_count <=
               library_count(order->library, LIBRARY_TRACKS);
}

bool album_order_begin(AlbumOrder *order, const Library *library, BrowseQuery query, uint32_t seed)
{
    *order = (AlbumOrder){.library = library, .shuffled = seed != 0};
    order->failed = query.kind != BROWSE_GROUP_ALBUMS ||
                    (query.group != LIBRARY_ARTISTS && query.group != LIBRARY_ALBUM_ARTISTS &&
                     query.group != LIBRARY_GENRES) ||
                    !library_read_group(library, query.group, query.subject, &order->group);
    if (!order->failed) {
        order->permutation = permutation_make(order->group.link_count, seed);
    }
    return !order->failed;
}

bool album_order_step(AlbumOrder *order)
{
    if (order->ready || order->failed) {
        return !order->failed;
    }
    if (order->scanned == order->group.link_count) {
        order->ready = order->count != 0;
        order->failed = !order->ready;
        return order->ready;
    }
    LibraryAlbum album;
    if (!read_album(order, order->scanned++, &album) ||
        order->count + album.track_count > UINT16_MAX) {
        order->failed = true;
        return false;
    }
    order->count += album.track_count;
    return true;
}

static bool seek_album(AlbumOrder *order, uint32_t position)
{
    if (position < order->cursor) {
        order->cursor = 0;
        order->album_position = 0;
        order->album.track_count = 0;
    }
    while (position >= order->cursor + order->album.track_count) {
        order->cursor += order->album.track_count;
        if (!read_album(order, order->album_position++, &order->album)) {
            return false;
        }
    }
    return true;
}

bool album_order_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    AlbumOrder *order = context;
    if (!order->ready || order->failed || (uint32_t)first + count > order->count) {
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        uint32_t position = (uint32_t)first + index;
        if (!seek_album(order, position)) {
            return false;
        }
        tracks[index] = (uint16_t)(order->album.first_track + position - order->cursor);
    }
    return true;
}
