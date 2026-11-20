#include "artist_album.h"

bool artist_album_only(const BrowseScreen *browse, BrowseQuery albums, BrowseQuery *album)
{
    const Library *library = browse->pool->config.library;
    BrowseItem only;
    return browse_count(library, albums) == 1 && browse_item(library, albums, 0, &only) &&
           browse_child(albums, only, album);
}
