#include "cover_cache.h"

#include "app/debug_loop.h"

static int slot_of(int position)
{
    return position % COVER_CACHE_SLOTS;
}

static CoverState state_of(CoverState state, Image *image, Image loaded)
{
    if (state != COVER_MISSING) {
        *image = loaded;
    }
    return state;
}

CoverState cover_cache_half(const CoverCache *cache, int position, uint16_t album, Image *image)
{
    const CoverSlot *slot = &cache->halves[slot_of(position)];
    if (slot->position != position || slot->album != album) {
        return COVER_EMPTY;
    }
    Image loaded = {.pixels = slot->pixels, .width = COVER_HALF_SIZE, .height = COVER_HALF_SIZE};
    return state_of(slot->state, image, loaded);
}

bool cover_cache_load_half(CoverCache *cache, int position, uint16_t album)
{
    Image ignored;
    if (cover_cache_half(cache, position, album, &ignored) != COVER_EMPTY) {
        return false;
    }
    CoverSlot *slot = &cache->halves[slot_of(position)];
    slot->position = position;
    slot->album = album;
    slot->state = cover_cache_read(cache, album, COVER_HALF, slot->pixels);
    debug_loop_event(DEBUG_EVENT_COVER,
                     album | (slot->state == COVER_LOADED ? DEBUG_COVER_FOUND : 0u));
    return true;
}
