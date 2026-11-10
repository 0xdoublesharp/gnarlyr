#include "discovery_preferences.h"

#include <stddef.h>

#include "core/hash.h"
#include "core/string_builder.h"
#include "discovery_screen.h"
#include "hal/storage.h"

enum {
    PREFERENCES_BYTES = 112,
    PREFERENCES_HASH_AT = 108,
    PREFERENCES_TEXT_AT = 44,
    PREFERENCES_VERSION = 1
};

static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/discovery.bin";

static bool tags_ready(const PersonalTags *tags)
{
    return tags != NULL && (tags->load == STORE_READY || tags->load == STORE_EMPTY) &&
           tags->result != STORE_WORKING;
}

static uint32_t tag_binding(const PersonalTags *tags)
{
    uint8_t active[4];
    store_put32(active, tags->catalog.active);
    uint32_t hash = hash_update(HASH_START, active, sizeof active);
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        if ((tags->catalog.active & (UINT32_C(1) << tag)) != 0) {
            hash = hash_update(hash, tags->catalog.names[tag], PERSONAL_TAG_NAME_BYTES);
        }
    }
    return hash;
}

void discovery_preferences_validate_tags(DiscoveryScreen *screen, const PersonalTags *tags)
{
    if (!tags_ready(tags)) {
        return;
    }
    uint32_t binding = tag_binding(tags);
    if (screen->tag_binding != binding) {
        screen->required_tags = 0;
    }
    screen->required_tags &= tags->catalog.active;
    screen->tag_binding = binding;
}

static bool valid_bytes(const uint8_t *bytes)
{
    if (bytes[0] != 'D' || bytes[1] != 'S' || bytes[2] != 'D' || bytes[3] != 'V' ||
        bytes[4] != PREFERENCES_VERSION || bytes[5] > LIBRARY_VIEW_SORT_DURATION || bytes[6] > 1 ||
        store_get32(bytes + PREFERENCES_HASH_AT) !=
            hash_update(HASH_START, bytes, PREFERENCES_HASH_AT)) {
        return false;
    }
    for (int index = 0; index < LIBRARY_VIEW_TEXT_BYTES; index++) {
        if (bytes[PREFERENCES_TEXT_AT + index] == 0) {
            return true;
        }
    }
    return false;
}

static bool valid_query(const LibraryViewConfig *query)
{
    return query->fields != 0 && (query->fields & ~(uint32_t)LIBRARY_VIEW_FIELDS_ALL) == 0 &&
           (query->year_min == 0 || query->year_max == 0 || query->year_min <= query->year_max) &&
           (query->duration_min_ms == 0 || query->duration_max_ms == 0 ||
            query->duration_min_ms <= query->duration_max_ms);
}

bool discovery_preferences_load(DiscoveryScreen *screen, const PersonalTags *tags)
{
    uint8_t bytes[PREFERENCES_BYTES];
    if (!hal_internal_file_read(PATH, bytes, sizeof bytes) || !valid_bytes(bytes)) {
        return false;
    }
    LibraryViewConfig query = library_view_defaults();
    query.source = screen->query.source;
    query.sort = (LibraryViewSort)bytes[5];
    query.descending = bytes[6] != 0;
    query.fields = store_get32(bytes + 12);
    query.year_min = store_get16(bytes + 22);
    query.year_max = store_get16(bytes + 24);
    query.duration_min_ms = store_get32(bytes + 28);
    query.duration_max_ms = store_get32(bytes + 32);
    StringBuilder text = string_builder_make(query.text, sizeof query.text);
    string_builder_append(&text, (const char *)(bytes + PREFERENCES_TEXT_AT));
    bool bound = screen->config.library != NULL && screen->config.library->content_known &&
                 store_get32(bytes + 8) == library_fingerprint(screen->config.library);
    if (bound) {
        query.artist = store_get16(bytes + 16);
        query.album = store_get16(bytes + 18);
        query.genre = store_get16(bytes + 20);
        const Library *library = screen->config.library;
        if (query.artist >= library_count(library, LIBRARY_ARTISTS)) {
            query.artist = LIBRARY_NONE;
        }
        if (query.album >= library_count(library, LIBRARY_ALBUMS)) {
            query.album = LIBRARY_NONE;
        }
        if (query.genre >= library_count(library, LIBRARY_GENRES)) {
            query.genre = LIBRARY_NONE;
        }
    }
    if (!valid_query(&query)) {
        return false;
    }
    screen->query = query;
    screen->required_tags = store_get32(bytes + 36);
    screen->tag_binding = store_get32(bytes + 40);
    discovery_preferences_validate_tags(screen, tags);
    return true;
}

bool discovery_preferences_save(const DiscoveryScreen *screen, const PersonalTags *tags)
{
    if (!tags_ready(tags) || !valid_query(&screen->query)) {
        return false;
    }
    uint8_t bytes[PREFERENCES_BYTES] = {0};
    bytes[0] = 'D';
    bytes[1] = 'S';
    bytes[2] = 'D';
    bytes[3] = 'V';
    bytes[4] = PREFERENCES_VERSION;
    bytes[5] = (uint8_t)screen->query.sort;
    bytes[6] = screen->query.descending;
    store_put32(bytes + 8, library_fingerprint(screen->config.library));
    store_put32(bytes + 12, screen->query.fields);
    store_put16(bytes + 16, screen->query.artist);
    store_put16(bytes + 18, screen->query.album);
    store_put16(bytes + 20, screen->query.genre);
    store_put16(bytes + 22, screen->query.year_min);
    store_put16(bytes + 24, screen->query.year_max);
    store_put32(bytes + 28, screen->query.duration_min_ms);
    store_put32(bytes + 32, screen->query.duration_max_ms);
    store_put32(bytes + 36, screen->required_tags & tags->catalog.active);
    store_put32(bytes + 40, tag_binding(tags));
    StringBuilder text =
        string_builder_make((char *)(bytes + PREFERENCES_TEXT_AT), LIBRARY_VIEW_TEXT_BYTES);
    string_builder_append(&text, screen->query.text);
    store_put32(bytes + PREFERENCES_HASH_AT, hash_update(HASH_START, bytes, PREFERENCES_HASH_AT));
    HalReserveResult reserved = hal_internal_file_reserve(PATH, 4096);
    return (reserved == HAL_RESERVE_CREATED || reserved == HAL_RESERVE_EXISTS) &&
           hal_internal_file_write(PATH, bytes, sizeof bytes);
}
