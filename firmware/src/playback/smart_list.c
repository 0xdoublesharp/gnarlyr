#include "playback/smart_list.h"

#include <stddef.h>

enum {
    STAGE_TALLY,
    STAGE_JOIN,
    STAGE_DONE,
    KEY_PERCENT_SHIFT = 24,
    KEY_PERCENT_STEPS = 256,
    FULL_PERCENT = 100,
    LAST_WORKING_PERCENT = 99
};

static bool uses_history(SmartListKind kind)
{
    return kind != SMART_LIST_RECENTLY_ADDED;
}

bool smart_list_begin(SmartList *job, SmartListConfig config)
{
    bool history = uses_history(config.kind);
    *job = (SmartList){.config = config, .high = UINT32_MAX, .result = STORE_FAILED};
    if (config.keys == NULL || !config.keys->valid || config.kind >= SMART_LIST_KIND_COUNT ||
        (history &&
         (config.log == NULL || config.scratch.tallies == NULL || config.scratch.capacity == 0))) {
        return false;
    }
    if (history) {
        job->newest = config.log->root.next;
        listening_cursor_begin(&job->cursor, config.log, LISTENING_SECTOR_RECORDS);
    }
    job->stage = history ? STAGE_TALLY : STAGE_JOIN;
    job->result = STORE_WORKING;
    return true;
}

static void raise_percent(SmartList *job, uint32_t percent)
{
    if (percent > job->percent) {
        job->percent = (uint8_t)(percent < LAST_WORKING_PERCENT ? percent : LAST_WORKING_PERCENT);
    }
}

static uint32_t key_percent(uint32_t key)
{
    return (key >> KEY_PERCENT_SHIFT) * FULL_PERCENT / KEY_PERCENT_STEPS;
}

static uint16_t place_of(const SmartList *job, uint32_t key)
{
    uint16_t low = 0;
    uint16_t high = job->used;
    while (low < high) {
        uint16_t middle = (uint16_t)((low + high) / 2);
        if (job->config.scratch.tallies[middle].key < key) {
            low = (uint16_t)(middle + 1);
        } else {
            high = middle;
        }
    }
    return low;
}

static SmartTally *claim(SmartList *job, uint32_t key)
{
    SmartTally *tallies = job->config.scratch.tallies;
    uint16_t place = place_of(job, key);
    if (place < job->used && tallies[place].key == key) {
        return &tallies[place];
    }
    bool full = job->used == job->config.scratch.capacity;
    job->more |= full;
    if (full && place == job->used) {
        return NULL;
    }
    uint16_t last = (uint16_t)(full ? job->used - 1 : job->used);
    for (uint16_t index = last; index > place; index--) {
        tallies[index] = tallies[index - 1];
    }
    job->used = (uint16_t)(full ? job->used : job->used + 1);
    tallies[place] = (SmartTally){.key = key};
    return &tallies[place];
}

static void visit(void *context, const ListeningRecord *record)
{
    SmartList *job = context;
    uint32_t key = record->play.track.key;
    SmartTally *tally = key >= job->low ? claim(job, key) : NULL;
    if (tally == NULL) {
        return;
    }
    tally->plays = (uint16_t)(tally->plays + (tally->plays < UINT16_MAX ? 1 : 0));
    bool listened = listening_play_listened(&record->play) && tally->listens < UINT16_MAX;
    tally->listens = (uint16_t)(tally->listens + (listened ? 1 : 0));
    tally->last = record->sequence;
}

static StoreResult step_tally(SmartList *job)
{
    StoreResult result = listening_cursor_step(&job->cursor, (ListeningVisitor){visit, job});
    uint32_t base = key_percent(job->low);
    uint32_t kept = job->cursor.kept;
    raise_percent(
        job, kept == 0 ? base : base + (FULL_PERCENT - base) * job->cursor.position / (2 * kept));
    if (result != STORE_DONE) {
        return result;
    }
    job->high = job->more ? job->config.scratch.tallies[job->used - 1].key : UINT32_MAX;
    job->matched = 0;
    job->stage = STAGE_JOIN;
    return STORE_WORKING;
}

static bool listed_track(const SmartFavorites *favorites, uint16_t track, uint32_t *place)
{
    uint16_t low = 0;
    uint16_t high = favorites->track_count;
    while (low < high) {
        uint16_t middle = (uint16_t)((low + high) / 2);
        if (favorites->tracks[middle].track < track) {
            low = (uint16_t)(middle + 1);
        } else {
            high = middle;
        }
    }
    bool found = low < favorites->track_count && favorites->tracks[low].track == track;
    *place = found ? favorites->tracks[low].place : 0;
    return found;
}

static bool album_track(const SmartFavorites *favorites, uint16_t track, uint32_t *place)
{
    uint16_t low = 0;
    uint16_t high = favorites->album_count;
    while (low < high) {
        uint16_t middle = (uint16_t)((low + high) / 2);
        if (favorites->albums[middle].first <= track) {
            low = (uint16_t)(middle + 1);
        } else {
            high = middle;
        }
    }
    const SmartFavoriteAlbum *album = low > 0 ? &favorites->albums[low - 1] : NULL;
    bool found = album != NULL && (uint32_t)track - album->first < album->count;
    *place = found ? album->place + (uint32_t)(track - album->first) : 0;
    return found;
}

static bool favorite_place(const SmartFavorites *favorites, uint16_t track, uint32_t *place)
{
    return listed_track(favorites, track, place) || album_track(favorites, track, place);
}

static bool arrival_rank(TrackKey entry, bool wanted, SmartPick *pick)
{
    pick->primary = (uint32_t)UINT16_MAX - entry.arrival;
    pick->secondary = entry.track;
    return wanted;
}

static bool forgotten_rank(const SmartList *job, TrackKey entry, const SmartTally *tally,
                           SmartPick *pick)
{
    uint32_t age = tally != NULL ? job->newest - tally->last : UINT32_MAX;
    pick->primary = tally != NULL ? UINT32_MAX - age : UINT32_MAX;
    return age > SMART_LIST_FORGOTTEN_WINDOW &&
           favorite_place(&job->config.favorites, entry.track, &pick->secondary);
}

static bool rank(const SmartList *job, TrackKey entry, const SmartTally *tally, SmartPick *pick)
{
    uint32_t age = tally != NULL ? job->newest - tally->last : UINT32_MAX;
    switch (job->config.kind) {
    case SMART_LIST_RECENTLY_ADDED:
        return arrival_rank(entry, entry.arrival != 0, pick);
    case SMART_LIST_NEVER_PLAYED:
        return arrival_rank(entry, tally == NULL, pick);
    case SMART_LIST_RECENTLY_PLAYED:
        *pick = (SmartPick){.primary = age, .secondary = entry.track, .track = entry.track};
        return tally != NULL;
    case SMART_LIST_MOST_PLAYED:
        *pick = (SmartPick){.secondary = age, .track = entry.track};
        pick->primary = tally != NULL ? (uint32_t)UINT16_MAX - tally->listens : 0;
        return tally != NULL && tally->listens > 0;
    case SMART_LIST_FORGOTTEN:
        return forgotten_rank(job, entry, tally, pick);
    case SMART_LIST_KIND_COUNT:
        break;
    }
    return false;
}

static bool earlier(SmartPick first, SmartPick second)
{
    if (first.primary != second.primary) {
        return first.primary < second.primary;
    }
    if (first.secondary != second.secondary) {
        return first.secondary < second.secondary;
    }
    return first.track < second.track;
}

static void offer(SmartList *job, SmartPick pick)
{
    uint16_t count = job->count;
    if (count == SMART_LIST_CAPACITY && !earlier(pick, job->picks[count - 1])) {
        return;
    }
    uint16_t place = (uint16_t)(count < SMART_LIST_CAPACITY ? count : SMART_LIST_CAPACITY - 1);
    while (place > 0 && earlier(pick, job->picks[place - 1])) {
        job->picks[place] = job->picks[place - 1];
        place--;
    }
    job->picks[place] = pick;
    job->count = (uint16_t)(count < SMART_LIST_CAPACITY ? count + 1 : count);
}

static const SmartTally *tally_of(SmartList *job, uint32_t key)
{
    const SmartTally *tallies = job->config.scratch.tallies;
    if (!uses_history(job->config.kind)) {
        return NULL;
    }
    while (job->matched < job->used && tallies[job->matched].key < key) {
        job->matched++;
    }
    return job->matched < job->used && tallies[job->matched].key == key ? &tallies[job->matched]
                                                                        : NULL;
}

static void join(SmartList *job, TrackKey entry)
{
    if ((job->has_previous && entry.key == job->previous_key) ||
        (job->config.library != NULL && library_hidden(job->config.library, entry.track))) {
        return;
    }
    job->previous_key = entry.key;
    job->has_previous = true;
    SmartPick pick = {.track = entry.track};
    if (rank(job, entry, tally_of(job, entry.key), &pick)) {
        pick.track = entry.track;
        offer(job, pick);
    }
    raise_percent(job, key_percent(entry.key));
}

static StoreResult finish(SmartList *job)
{
    job->stage = STAGE_DONE;
    job->percent = FULL_PERCENT;
    return STORE_DONE;
}

static StoreResult next_pass(SmartList *job)
{
    job->low = job->high + 1;
    job->used = 0;
    job->more = false;
    listening_cursor_rewind(&job->cursor);
    job->stage = STAGE_TALLY;
    return STORE_WORKING;
}

static bool refill_block(SmartList *job)
{
    const TrackKeys *keys = job->config.keys;
    uint32_t left = keys->count - job->next_entry;
    uint16_t count = (uint16_t)(left < TRACK_KEYS_BLOCK_ENTRIES ? left : TRACK_KEYS_BLOCK_ENTRIES);
    if (!track_keys_read(keys, job->next_entry, count, job->block)) {
        return false;
    }
    job->next_entry += count;
    job->block_at = 0;
    job->block_held = count;
    return true;
}

static StoreResult step_join(SmartList *job)
{
    if (job->block_at == job->block_held) {
        if (job->next_entry == job->config.keys->count) {
            return finish(job);
        }
        if (!refill_block(job)) {
            return STORE_FAILED;
        }
    }
    while (job->block_at < job->block_held) {
        TrackKey entry = job->block[job->block_at];
        if (entry.key > job->high) {
            return next_pass(job);
        }
        job->block_at++;
        join(job, entry);
    }
    return STORE_WORKING;
}

StoreResult smart_list_step(SmartList *job)
{
    if (job->result != STORE_WORKING) {
        return job->result;
    }
    job->result = job->stage == STAGE_TALLY ? step_tally(job) : step_join(job);
    return job->result;
}

uint16_t smart_list_track(const SmartList *job, uint16_t position)
{
    return position < job->count ? job->picks[position].track : LIBRARY_NONE;
}
