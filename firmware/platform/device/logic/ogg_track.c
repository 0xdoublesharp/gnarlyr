#include "device/ogg_track.h"

#include <stddef.h>

enum {
    MAX_PAGE_BYTES = 65307
};

static bool follows(const OggPage *page, uint32_t serial, uint32_t sequence)
{
    return page->serial == serial && page->sequence == sequence && (page->flags & 2) == 0;
}

static OggPacketResult selected_page(FlacInput input, uint32_t offset, uint32_t serial,
                                     OggPage *page)
{
    while (offset < input.size) {
        OggPacketResult result = ogg_page_read(input, offset, page);
        if (result != OGG_PACKET_READY || page->serial == serial) {
            return result;
        }
        offset = page->next;
    }
    return OGG_PACKET_END;
}

static bool endpoint(FlacInput input, OggTrack *track, OggPage page)
{
    uint64_t granule = 0;
    for (;;) {
        if (page.serial != track->serial ||
            (page.granule != UINT64_MAX && (page.granule > UINT32_MAX || page.granule < granule))) {
            return false;
        }
        if (page.granule != UINT64_MAX) {
            granule = page.granule;
        }
        if ((page.flags & 4) != 0) {
            if (page.granule == UINT64_MAX || granule == 0) {
                return false;
            }
            track->samples = (uint32_t)granule;
            track->end_granule = (uint32_t)granule;
            track->stream_end = page.next;
            return true;
        }
        uint32_t sequence = page.sequence + 1;
        if (selected_page(input, page.next, track->serial, &page) != OGG_PACKET_READY ||
            !follows(&page, track->serial, sequence)) {
            return false;
        }
    }
}

bool ogg_track_endpoint(FlacInput input, OggTrack *track)
{
    OggPage page;
    return selected_page(input, 0, track->serial, &page) == OGG_PACKET_READY &&
           endpoint(input, track, page);
}

static bool marked_page(FlacInput input, const OggTrack *track, OggPage *page)
{
    if (page->serial != track->serial &&
        selected_page(input, page->next, track->serial, page) != OGG_PACKET_READY) {
        return false;
    }
    if (page->offset >= track->stream_end || (page->flags & 2) != 0) {
        return false;
    }
    while (page->granule == UINT64_MAX || page->last_complete == 0) {
        if ((page->flags & 4) != 0) {
            return page->segments == 0 &&
                   (page->granule == UINT64_MAX || page->granule <= track->end_granule);
        }
        uint32_t sequence = page->sequence + 1;
        if (selected_page(input, page->next, track->serial, page) != OGG_PACKET_READY ||
            !follows(page, track->serial, sequence)) {
            return false;
        }
    }
    return page->granule <= track->end_granule;
}

bool ogg_track_seek(FlacInput input, const OggTrack *track, uint32_t sample, uint32_t *offset)
{
    if (track == NULL || offset == NULL || track->samples == 0 || input.read == NULL ||
        input.size != track->file_bytes || track->data_offset >= input.size) {
        return false;
    }
    uint32_t target = sample < track->samples ? sample : track->samples - 1;
    if (target > UINT32_MAX - track->origin) {
        return false;
    }
    target += track->origin;
    uint32_t low = track->data_offset;
    uint32_t high = track->stream_end;
    uint32_t best = 0;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        uint32_t span = high - middle < MAX_PAGE_BYTES ? high - middle : MAX_PAGE_BYTES;
        OggPage page;
        OggPacketResult result = ogg_page_find(input, middle, middle + span, &page);
        if (result == OGG_PACKET_END) {
            high = middle;
            continue;
        }
        if (result != OGG_PACKET_READY) {
            return false;
        }
        uint32_t boundary = page.offset;
        if (!marked_page(input, track, &page)) {
            return false;
        }
        if (page.granule <= target && (page.flags & 4) == 0) {
            best = page.offset;
            low = page.next;
        } else {
            high = boundary;
        }
    }
    *offset = best;
    return true;
}
