#pragma once

#include <stdint.h>

#include "device/core1_control.h"
#include "device/core1_m4a.h"
#include "device/core1_stream.h"
#include "device/core1_vorbis.h"
#include "device/mp3_probe.h"

_Static_assert(CORE1_STREAM_SAMPLES +
                       sizeof(int32_t[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK]) <=
                   CORE1_STREAM_RING,
               "the core 1 output slots must end below the input ring");
_Static_assert(CORE1_STREAM_CONTROL + sizeof(StreamControl) <=
                   CORE1_SHARED_BASE + CORE1_SHARED_BYTES,
               "the stream control block must fit in the shared block");

static inline StreamRegion core1_region(void)
{
    return (StreamRegion){
        .control = (volatile StreamControl *)(uintptr_t)CORE1_STREAM_CONTROL,
        .samples = (int32_t (*)[FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK])(uintptr_t)CORE1_STREAM_SAMPLES,
        .ring = (volatile uint8_t *)(uintptr_t)CORE1_STREAM_RING,
        .ring_bytes = CORE1_STREAM_RING_BYTES,
    };
}

_Static_assert(M4A_RING_BYTES + M4A_HEAP_BYTES <= CORE1_STREAM_RING_BYTES,
               "the M4A ring and heap must fit the core 1 ring area");

static inline StreamRegion core1_region_m4a(void)
{
    StreamRegion region = core1_region();
    region.ring_bytes = M4A_RING_BYTES;
    return region;
}

static inline void *core1_m4a_heap(void)
{
    return (void *)(uintptr_t)(CORE1_STREAM_RING + M4A_RING_BYTES);
}

_Static_assert(MP3_RING_BYTES + sizeof(int32_t[FLAC_MAX_CHANNELS][MP3_TAIL_CAPACITY]) <=
                   CORE1_STREAM_RING_BYTES,
               "the MP3 ring and tail buffer must fit the core 1 ring area");

static inline StreamRegion core1_region_mp3(void)
{
    StreamRegion region = core1_region();
    region.ring_bytes = MP3_RING_BYTES;
    return region;
}

static inline void *core1_mp3_tail(void)
{
    return (void *)(uintptr_t)(CORE1_STREAM_RING + MP3_RING_BYTES);
}

_Static_assert(VORBIS_RING_BYTES + VORBIS_WORKSPACE_BYTES <= CORE1_STREAM_RING_BYTES,
               "the Vorbis stream and workspace must fit the core 1 ring area");

static inline StreamRegion core1_region_vorbis(void)
{
    StreamRegion region = core1_region();
    region.ring_bytes = VORBIS_RING_BYTES;
    return region;
}
