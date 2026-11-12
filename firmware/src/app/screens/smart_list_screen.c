#include "smart_list_screen.h"
#include "listening_work.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "ui/header.h"
#include "ui/list_navigation.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    STATUS_WORKING,
    STATUS_READY,
    STATUS_FAILED,
    STAGE_NONE,
    STAGE_UPDATE,
    STAGE_RANK,
    STEPS_PER_UPDATE = 64,
    STEP_BUDGET_MS = 12,
    PUBLISH_STEPS = 256,
    MESSAGE_BYTES = 96
};

static SmartListWork *const work = &listening_module_work.as.smart;

static const StringId TITLES[SMART_LIST_KIND_COUNT] = {
    STR_RECENTLY_ADDED, STR_RECENTLY_PLAYED,     STR_MOST_PLAYED,
    STR_NEVER_PLAYED,   STR_FORGOTTEN_FAVORITES,
};

static const StringId EMPTY[SMART_LIST_KIND_COUNT] = {
    STR_SMART_NONE_ADDED, STR_SMART_NONE_PLAYED,    STR_SMART_NONE_LISTENED,
    STR_SMART_ALL_PLAYED, STR_SMART_NONE_FORGOTTEN,
};

static const BrowsePoolConfig *pool_of(const SmartListScreen *screen)
{
    return &screen->config.browse->config;
}

static const Library *library_of(const SmartListScreen *screen)
{
    return pool_of(screen)->library;
}

static const SmartListCache *cache_of(const SmartListScreen *screen)
{
    return &screen->caches[screen->kind];
}

static bool owns_work(const SmartListScreen *screen)
{
    return listening_module_work.owner == screen;
}

static void claim_work(SmartListScreen *screen)
{
    listening_module_work.owner = screen;
    work->stage = STAGE_NONE;
    work->rows_ready = false;
}

static uint16_t shown_count(const SmartListScreen *screen)
{
    return screen->status == STATUS_READY ? cache_of(screen)->count : 0;
}

static SmartListStamp current_stamp(const SmartListScreen *screen)
{
    const LibraryExtraPlaylist *extra = library_of(screen)->extra;
    const FavoritesStore *favorites = pool_of(screen)->favorites;
    return (SmartListStamp){.log = screen->config.listening->log.generation,
                            .keys = work->keys.generation,
                            .binding = work->keys.binding,
                            .favorites = favorites != NULL ? favorites->revision : 0,
                            .extra = extra != NULL ? extra->count : 0};
}

static bool same_stamp(SmartListStamp first, SmartListStamp second)
{
    return first.log == second.log && first.keys == second.keys &&
           first.binding == second.binding && first.favorites == second.favorites &&
           first.extra == second.extra;
}

static void fail(SmartListScreen *screen)
{
    work->stage = STAGE_NONE;
    screen->status = STATUS_FAILED;
}

static void add_listed(SmartListFavorites *favorites, uint16_t *count, SmartFavoriteTrack entry)
{
    uint16_t place = *count;
    while (place > 0 && favorites->tracks[place - 1].track > entry.track) {
        favorites->tracks[place] = favorites->tracks[place - 1];
        place--;
    }
    favorites->tracks[place] = entry;
    (*count)++;
}

static void add_album(SmartListFavorites *favorites, uint16_t *count, SmartFavoriteAlbum entry)
{
    uint16_t place = *count;
    while (place > 0 && favorites->albums[place - 1].first > entry.first) {
        favorites->albums[place] = favorites->albums[place - 1];
        place--;
    }
    favorites->albums[place] = entry;
    (*count)++;
}

static void keep_audio_fed(void *context)
{
    (void)context;
    hal_audio_service();
}

static uint16_t gather_albums(const SmartListScreen *screen, SmartListFavorites *favorites)
{
    const FavoritesStore *store = pool_of(screen)->favorites;
    const Library *library = library_of(screen);
    uint16_t count = 0;
    uint32_t place = LIBRARY_EXTRA_CAPACITY;
    if (store == NULL) {
        return 0;
    }
    FavoritesCollector collector = {work->scratch.albums, FAVORITES_CAPACITY, keep_audio_fed, NULL};
    uint16_t albums = favorites_collect(&store->favorites, library, collector);
    for (uint16_t index = 0; index < albums; index++) {
        LibraryAlbum album;
        if (library_read_album(library, work->scratch.albums[index], &album)) {
            add_album(favorites, &count,
                      (SmartFavoriteAlbum){album.first_track, album.track_count, place});
            place += album.track_count;
        }
    }
    return count;
}

static SmartFavorites gather_favorites(const SmartListScreen *screen)
{
    SmartListFavorites *favorites = &work->job.ranking.favorites;
    const LibraryExtraPlaylist *extra = library_of(screen)->extra;
    uint16_t tracks = 0;
    if (screen->kind != SMART_LIST_FORGOTTEN) {
        return (SmartFavorites){0};
    }
    for (uint16_t index = 0; extra != NULL && index < extra->count; index++) {
        add_listed(favorites, &tracks, (SmartFavoriteTrack){extra->tracks[index], index});
    }
    uint16_t albums = gather_albums(screen, favorites);
    return (SmartFavorites){favorites->tracks, tracks, favorites->albums, albums};
}

static void begin_rank(SmartListScreen *screen)
{
    SmartListRanking *ranking = &work->job.ranking;
    screen->checking = false;
    screen->percent = 0;
    ranking->stamp = current_stamp(screen);
    SmartListConfig config = {.kind = screen->kind,
                              .log = &screen->config.listening->log,
                              .keys = &work->keys,
                              .favorites = gather_favorites(screen),
                              .scratch = {work->scratch.tallies, SMART_LIST_TALLIES},
                              .library = library_of(screen)};
    if (!smart_list_begin(&ranking->list, config)) {
        fail(screen);
        return;
    }
    work->stage = STAGE_RANK;
}

static void begin_update(SmartListScreen *screen)
{
    TrackKeysWork lent = {work->scratch.entries, SMART_LIST_WORK_ENTRIES};
    screen->checking = true;
    screen->percent = 0;
    if (!track_keys_update_begin(&work->job.update, &work->keys, library_of(screen), lent)) {
        fail(screen);
        return;
    }
    work->stage = STAGE_UPDATE;
}

static void start_work(SmartListScreen *screen)
{
    claim_work(screen);
    screen->status = STATUS_WORKING;
    StoreFile file = listening_store_file(&work->file, TRACK_KEYS_PATH, TRACK_KEYS_FILE_BYTES);
    if (store_blocking(track_keys_open(&work->keys, file))) {
        fail(screen);
    } else if (!track_keys_current(&work->keys, library_of(screen))) {
        begin_update(screen);
    } else if (cache_of(screen)->valid &&
               same_stamp(cache_of(screen)->stamp, current_stamp(screen))) {
        screen->status = STATUS_READY;
    } else {
        begin_rank(screen);
    }
}

static void keep_result(SmartListScreen *screen)
{
    const SmartList *list = &work->job.ranking.list;
    SmartListCache *cache = &screen->caches[screen->kind];
    cache->count = list->count;
    for (uint16_t position = 0; position < list->count; position++) {
        cache->tracks[position] = smart_list_track(list, position);
    }
    cache->stamp = work->job.ranking.stamp;
    cache->valid = true;
    work->stage = STAGE_NONE;
    work->rows_ready = false;
    screen->status = STATUS_READY;
    int last = list->count > 0 ? list->count - 1 : 0;
    list_view_select(&screen->list, screen->list.selected < last ? screen->list.selected : last,
                     screen->now_ms);
}

static void step_update(SmartListScreen *screen)
{
    StoreResult result = track_keys_update_step(&work->job.update);
    screen->percent = track_keys_update_progress(&work->job.update);
    if (result == STORE_DONE) {
        begin_rank(screen);
    } else if (result != STORE_WORKING) {
        fail(screen);
    }
}

static void step_rank(SmartListScreen *screen)
{
    SmartList *list = &work->job.ranking.list;
    StoreResult result = smart_list_step(list);
    screen->percent = list->percent;
    if (result == STORE_DONE) {
        keep_result(screen);
    } else if (result != STORE_WORKING) {
        fail(screen);
    }
}

static void run(SmartListScreen *screen)
{
    uint32_t started_ms = hal_clock_milliseconds();
    for (int step = 0; step < STEPS_PER_UPDATE && work->stage != STAGE_NONE &&
                       hal_clock_milliseconds() - started_ms < STEP_BUDGET_MS;
         step++) {
        if (work->stage == STAGE_UPDATE) {
            step_update(screen);
        } else {
            step_rank(screen);
        }
        hal_audio_service();
    }
}

static void describe(const SmartListScreen *screen, uint16_t track, char *label, char *detail)
{
    const Library *library = library_of(screen);
    LibraryTrack record;
    LibraryGroup artist;
    label[0] = '\0';
    detail[0] = '\0';
    if (!library_read_track(library, track, &record)) {
        return;
    }
    library_read_string(library, record.title, label, SMART_LIST_LABEL_BYTES);
    if (library_read_group(library, LIBRARY_ARTISTS, record.artist, &artist)) {
        library_read_string(library, artist.name, detail, SMART_LIST_DETAIL_BYTES);
    }
}

static void refresh_rows(SmartListScreen *screen)
{
    if (screen->status != STATUS_READY) {
        return;
    }
    if (!owns_work(screen)) {
        claim_work(screen);
    }
    if (work->rows_ready && work->first == screen->list.top) {
        return;
    }
    const SmartListCache *cache = cache_of(screen);
    work->first = screen->list.top;
    work->rows_ready = true;
    for (int row = 0; row < THEME_MAX_ROWS; row++) {
        int index = work->first + row;
        work->labels[row][0] = '\0';
        work->details[row][0] = '\0';
        if (index < cache->count) {
            describe(screen, cache->tracks[index], work->labels[row], work->details[row]);
        }
    }
}

static int cached_row(const SmartListScreen *screen, int index)
{
    int row = index - work->first;
    bool cached = owns_work(screen) && work->rows_ready && row >= 0 && row < THEME_MAX_ROWS;
    return cached ? row : -1;
}

static int row_count(const void *context)
{
    return shown_count(context);
}

static const char *row_label(const void *context, int index)
{
    int row = cached_row(context, index);
    return row >= 0 ? work->labels[row] : "";
}

static const char *row_detail(const void *context, int index)
{
    int row = cached_row(context, index);
    return row >= 0 && work->details[row][0] != '\0' ? work->details[row] : NULL;
}

static bool read_tracks(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    const SmartListCache *cache = context;
    if (first > cache->count || count > cache->count - first) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        tracks[index] = cache->tracks[first + index];
    }
    return true;
}

static bool is_loaded(const SmartListScreen *screen, uint16_t track)
{
    if (pool_of(screen)->player == NULL) {
        return false;
    }
    PlayerStatus status = player_status(pool_of(screen)->player);
    return status.has_track && status.state != PLAYER_IDLE && status.track == track;
}

static bool publish(SmartListScreen *screen, uint16_t position)
{
    SmartListCache *cache = &screen->caches[screen->kind];
    OrderPublish *job = &work->job.publish;
    TrackOrderSource source = {read_tracks, cache, cache->count};
    StoreResult result = STORE_FAILED;
    if (!owns_work(screen)) {
        claim_work(screen);
    }
    if (order_publish_begin(job, pool_of(screen)->player, source, position)) {
        result = STORE_WORKING;
        for (int step = 0; step < PUBLISH_STEPS && result == STORE_WORKING; step++) {
            result = order_publish_step(job);
        }
    }
    order_publish_cancel(job);
    return result == STORE_DONE;
}

static ScreenAction play_from(void *context, int index)
{
    SmartListScreen *screen = context;
    const BrowsePoolConfig *pool = pool_of(screen);
    uint16_t track = cache_of(screen)->tracks[index];
    if (is_loaded(screen, track) || publish(screen, (uint16_t)index)) {
        return screen_action_push(pool->now_playing);
    }
    if (pool->toast != NULL) {
        toast_show(pool->toast, i18n_text(STR_QUEUE_SAVE_FAILED), screen->now_ms);
    }
    return screen_action_none();
}

static ScreenAction song_actions(void *context, int index)
{
    SmartListScreen *screen = context;
    if (pool_of(screen)->player == NULL) {
        return screen_action_none();
    }
    Screen *actions =
        browse_pool_song_actions(screen->config.browse, cache_of(screen)->tracks[index],
                                 row_label(screen, index), screen->now_ms);
    return screen_action_push(actions);
}

static void abandon(SmartListScreen *screen)
{
    if (owns_work(screen) && work->stage == STAGE_UPDATE) {
        track_keys_update_abort(&work->job.update);
    }
    if (owns_work(screen)) {
        work->stage = STAGE_NONE;
    }
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    SmartListScreen *screen = context;
    screen->now_ms = now_ms;
    Navigation navigation = navigation_from_key(event);
    if (navigation.intent == NAVIGATION_BACK || navigation.intent == NAVIGATION_HOME) {
        abandon(screen);
    }
    ListSelection selection = {.select = play_from, .hold = song_actions, .context = screen};
    return list_navigation_handle(&screen->list, event, now_ms, selection);
}

static void update(void *context, uint32_t now_ms)
{
    SmartListScreen *screen = context;
    screen->now_ms = now_ms;
    if (screen->status == STATUS_WORKING && !owns_work(screen)) {
        start_work(screen);
    }
    if (screen->status == STATUS_WORKING) {
        run(screen);
    }
    refresh_rows(screen);
}

static void draw_message(const SmartListScreen *screen, Canvas *canvas, Rect content)
{
    const Theme *theme = pool_of(screen)->theme;
    char progress[MESSAGE_BYTES];
    StringBuilder builder = string_builder_make(progress, sizeof progress);
    i18n_format_counts(&builder, screen->checking ? STR_SMART_CHECKING : STR_SMART_FINDING,
                       screen->percent, 0);
    const char *text = screen->status == STATUS_FAILED  ? i18n_text(STR_HISTORY_UNAVAILABLE)
                       : screen->status == STATUS_READY ? i18n_text(EMPTY[screen->kind])
                                                        : progress;
    message_draw(canvas, theme, content, text);
}

static void draw(void *context, Canvas *canvas)
{
    SmartListScreen *screen = context;
    const Theme *theme = pool_of(screen)->theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, screen->screen.title);
    if (shown_count(screen) == 0) {
        draw_message(screen, canvas, content);
        return;
    }
    list_view_draw(&screen->list, canvas, content, screen->now_ms);
}

static bool working(const SmartListScreen *screen)
{
    return screen->status == STATUS_WORKING && owns_work(screen) && work->stage != STAGE_NONE;
}

static void enter(void *context, uint32_t now_ms)
{
    SmartListScreen *screen = context;
    screen->now_ms = now_ms;
    if (!working(screen)) {
        start_work(screen);
    }
    list_view_fit(&screen->list);
    list_view_restart_marquee(&screen->list, now_ms);
    refresh_rows(screen);
}

static ScreenAction open_kind(void *context)
{
    SmartListEntry *entry = context;
    SmartListScreen *screen = entry->screen;
    abandon(screen);
    screen->kind = entry->kind;
    screen->status = STATUS_WORKING;
    screen->screen.title = i18n_text(TITLES[entry->kind]);
    list_view_init(&screen->list, &screen->model, pool_of(screen)->theme, screen->menu.now_ms);
    return screen_action_push(&screen->screen);
}

static void build_menu(SmartListScreen *screen)
{
    for (int kind = 0; kind < SMART_LIST_KIND_COUNT; kind++) {
        screen->entries[kind] = (SmartListEntry){screen, (SmartListKind)kind};
        screen->items[kind] = (MenuItem){.label = i18n_text(TITLES[kind]),
                                         .select = open_kind,
                                         .context = &screen->entries[kind],
                                         .opens = true};
    }
    menu_screen_init(&screen->menu, (MenuConfig){.title = i18n_text(STR_SMART_LISTS),
                                                 .items = screen->items,
                                                 .item_count = SMART_LIST_KIND_COUNT,
                                                 .theme = pool_of(screen)->theme});
}

Screen *smart_list_screen_init(SmartListScreen *screen, SmartListScreenConfig config)
{
    *screen = (SmartListScreen){.config = config};
    screen->model = (ListModel){.context = screen,
                                .count = row_count,
                                .label = row_label,
                                .detail = row_detail,
                                .detail_below = true};
    screen->screen = (Screen){.title = i18n_text(STR_SMART_LISTS),
                              .context = screen,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw};
    build_menu(screen);
    return &screen->menu.screen;
}
