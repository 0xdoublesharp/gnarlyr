#pragma once

#include "playback/mix_resolve.h"

typedef struct MixApi {
    MixLoad (*catalog_open)(MixCatalog *catalog, MixIO io);
    MixLoad (*catalog_previous)(MixCatalog *catalog);
    bool (*catalog_read)(const MixCatalog *catalog, uint16_t slot, MixEntry *entry);
    bool (*catalog_begin)(MixCatalogJob *job, MixCatalog *catalog, uint32_t revision,
                          MixPatch patch);
    MixResult (*catalog_step)(MixCatalogJob *job);
    void (*catalog_abort)(MixCatalogJob *job);
    MixLoad (*identity_read)(const Library *library, uint16_t track, MixIdentity *identity);
    bool (*identity_equal)(const MixIdentity *a, const MixIdentity *b);
    uint32_t (*identity_hash)(const MixIdentity *identity);
    void (*measure_begin)(MixMeasure *measure, MixSource source);
    MixResult (*measure_step)(MixMeasure *measure);
    bool (*tape_write_begin)(MixTapeWrite *job, MixIO io, const MixMeasure *measured,
                             uint32_t generation);
    MixResult (*tape_write_step)(MixTapeWrite *job);
    void (*tape_write_abort)(MixTapeWrite *job);
    void (*tape_check_begin)(MixTapeCheck *job, MixTape *tape, MixIO io, uint32_t generation);
    MixResult (*tape_check_step)(MixTapeCheck *job);
    void (*tape_cursor)(MixTapeCursor *cursor, const MixTape *tape);
    MixLoad (*tape_next)(MixTapeCursor *cursor, MixIdentity *identity);
    bool (*resolve_begin)(MixResolve *job, const Library *library, const MixTape *tape,
                          MixMemory memory);
    MixResult (*resolve_step)(MixResolve *job);
    TrackOrderSource (*resolve_source)(const MixResolve *job);
} MixApi;

const MixApi *mix_code_api(void);
