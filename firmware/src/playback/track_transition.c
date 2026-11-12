#include "playback/track_transition.h"

bool track_transition_album_order(const LibraryTrack *from, const LibraryTrack *to)
{
    if (from->album != to->album) {
        return false;
    }
    if (from->number == 0 || to->number == 0) {
        return true;
    }
    bool next_number = to->disc == from->disc && to->number == from->number + 1;
    bool next_disc = to->disc == from->disc + 1 && to->number == 1;
    return next_number || next_disc;
}

static bool excluded(const TransitionRequest *request)
{
    const LibraryTrack *from = &request->from.record;
    const LibraryTrack *to = &request->to.record;
    return !request->fade || request->shuffle == SHUFFLE_OFF || request->from.spoken ||
           request->to.spoken || from->path == to->path || track_transition_album_order(from, to);
}

uint32_t track_transition_fade_ms(const TransitionRequest *request)
{
    if (excluded(request)) {
        return 0;
    }
    uint32_t from_ms = request->from.record.duration_ms;
    uint32_t to_ms = request->to.record.duration_ms;
    uint32_t shorter = from_ms < to_ms ? from_ms : to_ms;
    if (shorter < TRACK_TRANSITION_MIN_TRACK_MS) {
        return 0;
    }
    uint32_t fade_ms = (uint32_t)request->tenths * TRACK_TRANSITION_MS_PER_TENTH;
    uint32_t limit = shorter / TRACK_TRANSITION_TRACK_DIVISOR;
    return fade_ms < limit ? fade_ms : limit;
}
