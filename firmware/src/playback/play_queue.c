#include "playback/play_queue.h"

#include <stddef.h>

#include "library/library_content.h"

enum {
    MOST_ENTRIES = UINT16_MAX,
    STREAM_SCAN_ALBUMS = 16
};

static bool shuffles_albums(const PlayQueue *queue)
{
    return queue->stored_order == NULL && queue->plan.shuffle == SHUFFLE_ALBUMS;
}

static bool anchor_in_list(const PlayQueue *queue)
{
    return queue->anchor_base < queue->base_count;
}

static bool source_track(const PlayQueue *queue, uint32_t position, uint16_t *track)
{
    BrowseItem item;
    if (!browse_item(queue->library, queue->plan.source, position, &item) ||
        item.kind != BROWSE_ITEM_TRACK) {
        return false;
    }
    *track = item.index;
    return true;
}

static bool base_value(const PlayQueue *queue, uint32_t base, uint16_t *value)
{
    if (base >= queue->base_count) {
        return false;
    }
    if (shuffles_albums(queue)) {
        *value = (uint16_t)base;
        return true;
    }
    return source_track(queue, queue->first_track + base, value);
}

static uint32_t first_track_position(const PlayQueue *queue, uint32_t available)
{
    uint16_t track = 0;
    uint32_t position = 0;
    while (position < available && !source_track(queue, position, &track)) {
        position++;
    }
    return position;
}

static uint16_t find_anchor(const PlayQueue *queue)
{
    uint16_t track = 0;
    for (uint16_t base = 0; base < queue->base_count; base++) {
        if (base_value(queue, base, &track) && track == queue->anchor_value) {
            return base;
        }
    }
    return queue->base_count;
}

static void finish_layout(PlayQueue *queue)
{
    bool shuffled = queue->plan.shuffle != SHUFFLE_OFF;
    if (!anchor_in_list(queue) && queue->base_count == MOST_ENTRIES) {
        queue->base_count--;
        queue->anchor_base = queue->base_count;
    }
    queue->count = anchor_in_list(queue) ? queue->base_count : (uint16_t)(queue->base_count + 1);
    queue->cursor.position = !shuffled && anchor_in_list(queue) ? queue->anchor_base : 0;
    queue->order = permutation_make(shuffled ? (uint32_t)queue->count - 1 : 0, queue->plan.seed);
}

static bool build_track_queue(PlayQueue *queue)
{
    uint32_t available = browse_count(queue->library, queue->plan.source);
    queue->first_track = first_track_position(queue, available);
    uint32_t tracks = available - queue->first_track;
    queue->base_count = (uint16_t)(tracks < MOST_ENTRIES ? tracks : MOST_ENTRIES);
    queue->anchor_value = queue->plan.anchor_track;
    queue->anchor_base = find_anchor(queue);
    finish_layout(queue);
    return true;
}

static bool build_album_queue(PlayQueue *queue)
{
    LibraryTrack anchor;
    LibraryAlbum album;
    uint32_t albums = library_music_count(queue->library, LIBRARY_ALBUMS);
    uint32_t tracks = library_count(queue->library, LIBRARY_TRACKS);
    if (!library_read_track(queue->library, queue->plan.anchor_track, &anchor) ||
        !library_read_album(queue->library, anchor.album, &album) || anchor.album >= albums ||
        queue->plan.anchor_track < album.first_track ||
        queue->plan.anchor_track >= (uint32_t)album.first_track + album.track_count ||
        (uint32_t)album.first_track + album.track_count > tracks) {
        return false;
    }
    queue->base_count = (uint16_t)(albums < MOST_ENTRIES ? albums : MOST_ENTRIES);
    queue->anchor_value = anchor.album;
    queue->anchor_base = anchor.album;
    finish_layout(queue);
    queue->cursor.album_track = (uint16_t)(queue->plan.anchor_track - album.first_track);
    return true;
}

bool play_queue_build(PlayQueue *queue, const Library *library, PlayQueuePlan plan)
{
    *queue = (PlayQueue){.library = library, .plan = plan};
    return shuffles_albums(queue) ? build_album_queue(queue) : build_track_queue(queue);
}

static bool order_matches(const Library *library, const TrackOrder *order)
{
    uint32_t fingerprint = 0;
    return order != NULL && order->valid && !order->needs_reload &&
           library_content_fingerprint(library, &fingerprint) &&
           order->config.fingerprint == fingerprint &&
           order->config.track_limit == library_count(library, LIBRARY_TRACKS);
}

bool play_queue_use_order(PlayQueue *queue, const Library *library, const TrackOrder *order,
                          PlayQueuePlan plan)
{
    if (!order_matches(library, order)) {
        return false;
    }
    *queue = (PlayQueue){.library = library,
                         .plan = plan,
                         .count = order->count,
                         .stored_order = order,
                         .stored_generation = order->generation};
    return true;
}

static bool stored_current(const PlayQueue *queue)
{
    const TrackOrder *order = queue->stored_order;
    return order == NULL || (order->valid && queue->stored_generation == order->generation);
}

static bool entry_at(const PlayQueue *queue, uint16_t position, uint16_t *value)
{
    if (position >= queue->count) {
        return false;
    }
    bool shuffled = queue->plan.shuffle != SHUFFLE_OFF;
    if (!shuffled && anchor_in_list(queue)) {
        return base_value(queue, position, value);
    }
    if (position == 0) {
        *value = queue->anchor_value;
        return true;
    }
    uint32_t base = (uint32_t)position - 1;
    if (shuffled) {
        base = permutation_at(&queue->order, base);
    }
    if (anchor_in_list(queue) && base >= queue->anchor_base) {
        base++;
    }
    return base_value(queue, base, value);
}

static bool album_at(const PlayQueue *queue, uint16_t position, LibraryAlbum *album)
{
    uint16_t index = 0;
    return entry_at(queue, position, &index) && library_read_album(queue->library, index, album);
}

static bool album_cursor(const PlayQueue *queue, PlayQueueCursor cursor, LibraryAlbum *album)
{
    return cursor.position < queue->count && album_at(queue, cursor.position, album) &&
           cursor.album_track < album->track_count &&
           (uint32_t)album->first_track + cursor.album_track <
               library_count(queue->library, LIBRARY_TRACKS);
}

static bool cursor_valid(const PlayQueue *queue, PlayQueueCursor cursor)
{
    if (!shuffles_albums(queue)) {
        return cursor.position < queue->count && cursor.album_track == 0;
    }
    LibraryAlbum album;
    return album_cursor(queue, cursor, &album);
}

bool play_queue_track_at(const PlayQueue *queue, PlayQueueCursor cursor, uint16_t *track)
{
    if (!stored_current(queue)) {
        return false;
    }
    if (queue->stored_order != NULL) {
        if (!cursor_valid(queue, cursor)) {
            return false;
        }
        return track_order_read(queue->stored_order, cursor.position, 1, track);
    }
    if (!shuffles_albums(queue)) {
        return cursor_valid(queue, cursor) && entry_at(queue, cursor.position, track);
    }
    LibraryAlbum album;
    if (!album_cursor(queue, cursor, &album)) {
        return false;
    }
    *track = (uint16_t)(album.first_track + cursor.album_track);
    return true;
}

bool play_queue_current(const PlayQueue *queue, uint16_t *track)
{
    return play_queue_track_at(queue, queue->cursor, track);
}

static bool neighbor_position(const PlayQueue *queue, uint16_t from, int direction, bool wrap,
                              uint16_t *result)
{
    int target = from + direction;
    if (target >= 0 && target < queue->count) {
        *result = (uint16_t)target;
        return true;
    }
    if (!wrap || queue->count == 0) {
        return false;
    }
    *result = (uint16_t)(target < 0 ? queue->count - 1 : 0);
    return true;
}

static bool album_neighbor(const PlayQueue *queue, PlayQueueCursor from, int direction, bool wrap,
                           PlayQueueCursor *result)
{
    LibraryAlbum album;
    if (!album_cursor(queue, from, &album)) {
        return false;
    }
    int track = from.album_track + direction;
    if (track >= 0 && track < album.track_count) {
        *result = (PlayQueueCursor){from.position, (uint16_t)track};
        return true;
    }
    uint16_t position = 0;
    if (!neighbor_position(queue, from.position, direction, wrap, &position) ||
        !album_at(queue, position, &album)) {
        return false;
    }
    uint16_t last_track = album.track_count > 0 ? (uint16_t)(album.track_count - 1) : 0;
    PlayQueueCursor next = {position, direction > 0 ? 0 : last_track};
    if (!album_cursor(queue, next, &album)) {
        return false;
    }
    *result = next;
    return true;
}

bool play_queue_step(const PlayQueue *queue, PlayQueueCursor from, int direction, bool wrap,
                     PlayQueueCursor *result)
{
    if (!stored_current(queue)) {
        return false;
    }
    if (shuffles_albums(queue)) {
        return album_neighbor(queue, from, direction, wrap, result);
    }
    if (!cursor_valid(queue, from)) {
        return false;
    }
    uint16_t position = 0;
    if (!neighbor_position(queue, from.position, direction, wrap, &position)) {
        return false;
    }
    *result = (PlayQueueCursor){position, 0};
    return true;
}

bool play_queue_neighbor(const PlayQueue *queue, int direction, bool wrap, PlayQueueCursor *result)
{
    return play_queue_step(queue, queue->cursor, direction, wrap, result);
}

bool play_queue_upcoming(const PlayQueue *queue, RepeatMode repeat, PlayQueueCursor *result)
{
    if (!stored_current(queue)) {
        return false;
    }
    if (repeat == REPEAT_ONE) {
        if (!cursor_valid(queue, queue->cursor)) {
            return false;
        }
        *result = queue->cursor;
        return queue->count > 0;
    }
    return play_queue_neighbor(queue, 1, repeat == REPEAT_ALL, result);
}

void play_queue_move_to(PlayQueue *queue, PlayQueueCursor cursor)
{
    queue->cursor = cursor;
}

PlayQueuePlace play_queue_place(const PlayQueue *queue)
{
    if (!stored_current(queue)) {
        return (PlayQueuePlace){0, 0};
    }
    if (!shuffles_albums(queue)) {
        if (!cursor_valid(queue, queue->cursor)) {
            return (PlayQueuePlace){0, 0};
        }
        return (PlayQueuePlace){(uint16_t)(queue->cursor.position + 1), queue->count};
    }
    LibraryAlbum album;
    if (!album_cursor(queue, queue->cursor, &album)) {
        return (PlayQueuePlace){0, 0};
    }
    return (PlayQueuePlace){(uint16_t)(queue->cursor.album_track + 1), album.track_count};
}

void play_queue_stream_init(PlayQueueStream *stream, const PlayQueue *queue)
{
    bool usable =
        stored_current(queue) && (queue->count == 0 || cursor_valid(queue, queue->cursor));
    *stream = (PlayQueueStream){.queue = *queue,
                                .count = queue->count,
                                .current = queue->cursor.position,
                                .result = shuffles_albums(queue) ? STORE_WORKING : STORE_DONE};
    if (!usable) {
        stream->result = STORE_FAILED;
    }
}

static bool scan_album(PlayQueueStream *stream)
{
    LibraryAlbum album;
    PlayQueueCursor cursor = stream->queue.cursor;
    if (!album_at(&stream->queue, stream->scan, &album) || album.track_count == 0 ||
        stream->total + album.track_count > MOST_ENTRIES) {
        return false;
    }
    if (stream->scan == cursor.position) {
        if (cursor.album_track >= album.track_count) {
            return false;
        }
        stream->current = (uint16_t)(stream->total + cursor.album_track);
    }
    stream->total += album.track_count;
    stream->scan++;
    return true;
}

StoreResult play_queue_stream_prepare(PlayQueueStream *stream)
{
    if (stream->result != STORE_WORKING) {
        return stream->result;
    }
    for (int step = 0; step < STREAM_SCAN_ALBUMS && stream->scan < stream->queue.count; step++) {
        if (!scan_album(stream)) {
            stream->result = STORE_FAILED;
            return stream->result;
        }
    }
    if (stream->scan == stream->queue.count) {
        stream->count = (uint16_t)stream->total;
        stream->result = STORE_DONE;
    }
    return stream->result;
}

static bool walk_album(PlayQueueStream *stream)
{
    PlayQueueWalk *walk = &stream->walk;
    if (!walk->has_album) {
        walk->has_album = album_at(&stream->queue, walk->cursor.position, &walk->album) &&
                          walk->album.track_count > 0;
    }
    return walk->has_album;
}

static void walk_advance(PlayQueueWalk *walk, uint16_t count)
{
    walk->position += count;
    walk->cursor.album_track = (uint16_t)(walk->cursor.album_track + count);
    if (walk->cursor.album_track == walk->album.track_count) {
        walk->cursor.position++;
        walk->cursor.album_track = 0;
        walk->has_album = false;
    }
}

static bool walk_to(PlayQueueStream *stream, uint16_t first)
{
    PlayQueueWalk *walk = &stream->walk;
    if (first < walk->position) {
        *walk = (PlayQueueWalk){0};
    }
    while (walk->position < first) {
        if (!walk_album(stream)) {
            return false;
        }
        uint32_t left = (uint32_t)walk->album.track_count - walk->cursor.album_track;
        uint32_t skip = first - walk->position;
        walk_advance(walk, (uint16_t)(skip < left ? skip : left));
    }
    return true;
}

static bool read_albums(PlayQueueStream *stream, uint16_t first, uint16_t count, uint16_t *tracks)
{
    PlayQueueWalk *walk = &stream->walk;
    uint32_t limit = library_count(stream->queue.library, LIBRARY_TRACKS);
    if (!walk_to(stream, first)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        if (!walk_album(stream)) {
            return false;
        }
        uint32_t track = (uint32_t)walk->album.first_track + walk->cursor.album_track;
        if (track >= limit || track >= MOST_ENTRIES) {
            return false;
        }
        tracks[index] = (uint16_t)track;
        walk_advance(walk, 1);
    }
    return true;
}

static bool read_entries(const PlayQueue *queue, uint16_t first, uint16_t count, uint16_t *tracks)
{
    if (queue->stored_order != NULL) {
        return stored_current(queue) && track_order_read(queue->stored_order, first, count, tracks);
    }
    for (uint16_t index = 0; index < count; index++) {
        if (!entry_at(queue, (uint16_t)(first + index), &tracks[index])) {
            return false;
        }
    }
    return true;
}

static bool stream_read(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    PlayQueueStream *stream = context;
    if (stream->result != STORE_DONE || first > stream->count || count > stream->count - first ||
        (count > 0 && tracks == NULL)) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    if (shuffles_albums(&stream->queue)) {
        return read_albums(stream, first, count, tracks);
    }
    return read_entries(&stream->queue, first, count, tracks);
}

TrackOrderSource play_queue_stream_source(PlayQueueStream *stream)
{
    return (TrackOrderSource){.read = stream_read, .context = stream, .count = stream->count};
}
