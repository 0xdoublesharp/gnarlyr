#include "library_store.h"

#include <stddef.h>

#include "favorite_playlist.h"

const char LIBRARY_STORE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/library.db";
const char HIDDEN_TRACKS_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/hidden.bin";

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const LibraryStore *store = context;
    return hal_file_read(store->file, offset, buffer, length);
}

static void attach_hidden(LibraryStore *store)
{
    HiddenTracksHead head = {.magic = 0};
    HalFile file;
    if (hal_file_open(&file, HIDDEN_TRACKS_PATH)) {
        (void)hal_file_read(file, 0, &head, sizeof head);
        hal_file_close(&file);
    }
    if (head.magic == HIDDEN_TRACKS_MAGIC &&
        head.fingerprint == library_fingerprint(&store->library) &&
        head.set.count <= LIBRARY_HIDDEN_CAPACITY) {
        store->hidden = head.set;
        store->library.hidden = &store->hidden;
    }
}

bool library_store_open(LibraryStore *store)
{
    store->available = false;
    store->library = (Library){.source = {.read = NULL, .context = NULL}};
    if (!hal_file_open(&store->file, LIBRARY_STORE_PATH)) {
        return false;
    }
    ByteSource file_source = {.read = read_file, .context = store};
    block_cache_init(&store->cache, file_source, store->file.size);
    LibraryStatus status =
        library_open(&store->library, block_cache_source(&store->cache), store->file.size);
    store->available = status == LIBRARY_OK;
    if (!store->available) {
        hal_file_close(&store->file);
        return false;
    }
    favorite_playlist_attach(&store->library);
    attach_hidden(store);
    return store->available;
}
