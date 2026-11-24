#include "library_store.h"

#include <stddef.h>

#include "favorite_playlist.h"
#include "store_file.h"

const char LIBRARY_STORE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/library.db";
const char HIDDEN_TRACKS_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/hidden.bin";

static void attach_hidden(LibraryStore *store)
{
    HiddenTracksHead head;
    HalFile file;
    if (!hal_file_open(&file, HIDDEN_TRACKS_PATH)) {
        return;
    }
    bool read = hal_file_read(file, 0, &head, sizeof head);
    hal_file_close(&file);
    if (read && head.magic == HIDDEN_TRACKS_MAGIC &&
        head.fingerprint == library_fingerprint(&store->library) &&
        head.set.count <= LIBRARY_HIDDEN_CAPACITY) {
        store->hidden = head.set;
        store->library.hidden = &store->hidden;
    }
}

bool library_store_open(LibraryStore *store)
{
    if (store->available) {
        hal_file_close(&store->file);
    }
    store->available = false;
    if (!hal_file_open(&store->file, LIBRARY_STORE_PATH)) {
        store->library = (Library){0};
        return false;
    }
    ByteSource file_source = {.read = store_file_read_open, .context = &store->file};
    block_cache_init(&store->cache, file_source, store->file.size);
    if (library_open(&store->library, block_cache_source(&store->cache), store->file.size) !=
        LIBRARY_OK) {
        hal_file_close(&store->file);
        return false;
    }
    store->available = true;
    favorite_playlist_attach(&store->library);
    attach_hidden(store);
    return true;
}
