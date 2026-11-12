#include "cover_cache.h"

#include "app/debug_loop.h"

const char COVER_CACHE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/covers.bin";

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const CoverCache *cache = context;
    return hal_file_read(cache->file, offset, buffer, length);
}

static void clear_slots(CoverCache *cache)
{
    for (int slot = 0; slot < COVER_CACHE_SLOTS; slot++) {
        cache->halves[slot].state = COVER_EMPTY;
        cache->halves[slot].position = -1;
    }
    cache->full_state = COVER_EMPTY;
}

void cover_cache_open(CoverCache *cache, const Library *library)
{
    if (cache->available) {
        hal_file_close(&cache->file);
    }
    cache->library = library;
    cache->available = false;
    clear_slots(cache);
    if (!hal_file_open(&cache->file, COVER_CACHE_PATH)) {
        return;
    }
    ByteSource source = {.read = read_file, .context = cache};
    cache->available = covers_open(&cache->covers, source, cache->file.size);
    if (!cache->available) {
        hal_file_close(&cache->file);
    }
}

static CoverState state_of(CoverState state, Image *image, Image loaded)
{
    if (state != COVER_MISSING) {
        *image = loaded;
    }
    return state;
}

CoverState cover_cache_full(const CoverCache *cache, uint16_t album, Image *image)
{
    if (cache->full_state == COVER_EMPTY || cache->full_album != album) {
        return COVER_EMPTY;
    }
    Image loaded = {.pixels = cache->full, .width = COVER_FULL_SIZE, .height = COVER_FULL_SIZE};
    return state_of(cache->full_state, image, loaded);
}

CoverState cover_cache_read(const CoverCache *cache, uint16_t album, CoverSize size, Color *pixels)
{
    uint32_t record = 0;
    uint32_t key = library_album_key(cache->library, album);
    bool found = cache->available && covers_find(&cache->covers, key, &record) &&
                 covers_read(&cache->covers, record, size, pixels);
    return found ? COVER_LOADED : COVER_MISSING;
}

bool cover_cache_load_full(CoverCache *cache, uint16_t album)
{
    Image ignored;
    if (cover_cache_full(cache, album, &ignored) != COVER_EMPTY) {
        return false;
    }
    cache->full_album = album;
    cache->full_state = cover_cache_read(cache, album, COVER_FULL, cache->full);
    debug_loop_event(DEBUG_EVENT_COVER,
                     album | DEBUG_COVER_FULL |
                         (cache->full_state == COVER_LOADED ? DEBUG_COVER_FOUND : 0u));
    return true;
}
