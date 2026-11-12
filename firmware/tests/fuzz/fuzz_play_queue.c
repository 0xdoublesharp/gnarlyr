#include <stddef.h>
#include <stdint.h>

#include "fixture_library.h"
#include "fuzz_source.h"
#include "playback/play_queue.h"

enum {
    ACTION_BYTES = 4,
    ACTION_LIMIT = 64,
    TRACK_BUFFER = 32
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static BrowseQuery query_at(uint8_t value)
{
    static const BrowseQuery QUERIES[] = {
        {BROWSE_ALL_SONGS, LIBRARY_ARTISTS, 0, LIBRARY_NONE},
        {BROWSE_ALBUM_TRACKS, LIBRARY_ARTISTS, 0, LIBRARY_NONE},
        {BROWSE_ALBUM_TRACKS, LIBRARY_ARTISTS, 3, LIBRARY_NONE},
        {BROWSE_FOLDER, LIBRARY_ARTISTS, 2, LIBRARY_NONE},
        {BROWSE_PLAYLIST_TRACKS, LIBRARY_ARTISTS, 0, LIBRARY_NONE},
        {BROWSE_SINGLE_TRACK, LIBRARY_ARTISTS, 0, LIBRARY_NONE},
    };
    return QUERIES[value % (sizeof QUERIES / sizeof QUERIES[0])];
}

static void check_stream(const PlayQueue *queue, uint8_t first, uint8_t requested)
{
    PlayQueueStream stream;
    play_queue_stream_init(&stream, queue);
    StoreResult result = STORE_WORKING;
    for (int step = 0; step < 16 && result == STORE_WORKING; step++) {
        result = play_queue_stream_prepare(&stream);
    }
    fuzz_require(result == STORE_DONE || result == STORE_FAILED);
    TrackOrderSource source = play_queue_stream_source(&stream);
    uint16_t tracks[TRACK_BUFFER];
    uint16_t count = requested % (TRACK_BUFFER + 1);
    uint16_t offset = source.count == 0 ? first : (uint16_t)(first % (source.count + 1));
    if (count > source.count - offset) {
        count = (uint16_t)(source.count - offset);
    }
    bool read = source.read(source.context, offset, count, tracks);
    fuzz_require((result == STORE_DONE) == read);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    uint8_t first = size > 0 ? data[0] : 0;
    uint8_t second = size > 1 ? data[1] : 0;
    PlayQueue queue;
    PlayQueuePlan plan = {
        .source = query_at(first),
        .anchor_track = (uint16_t)(second % (FIXTURE_TRACKS + 3)),
        .shuffle = (ShuffleMode)(first % SHUFFLE_MODE_COUNT),
        .seed = (uint32_t)first << 24 | (uint32_t)second << 16 | (uint32_t)size,
    };
    if (!play_queue_build(&queue, library, plan)) {
        return 0;
    }
    size_t actions = size / ACTION_BYTES;
    if (actions > ACTION_LIMIT) {
        actions = ACTION_LIMIT;
    }
    for (size_t index = 0; index < actions; index++) {
        const uint8_t *action = data + index * ACTION_BYTES;
        PlayQueueCursor cursor = {(uint16_t)action[0], (uint16_t)action[1]};
        PlayQueueCursor result;
        uint16_t track = 0;
        play_queue_track_at(&queue, cursor, &track);
        int direction = (action[2] & 1u) != 0 ? 1 : -1;
        if (play_queue_step(&queue, cursor, direction, (action[2] & 2u) != 0, &result) &&
            (action[2] & 4u) != 0) {
            play_queue_move_to(&queue, result);
        }
        play_queue_upcoming(&queue, (RepeatMode)(action[3] % REPEAT_MODE_COUNT), &result);
        PlayQueuePlace place = play_queue_place(&queue);
        fuzz_require(place.number <= place.total || place.total == 0);
    }
    check_stream(&queue, first, second);
    return 0;
}
