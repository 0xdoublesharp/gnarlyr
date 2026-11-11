#include "browse_screen.h"

#include <stdbool.h>
#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "favorite_playlist.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/message_screen.h"

enum {
    NO_LEVEL = -1
};

static const char STAR[] = "\xE2\x98\x85";

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static BrowseRows *rows_of_level(BrowsePool *pool, int level)
{
    for (int slot = 0; slot < BROWSE_ROW_CACHES; slot++) {
        if (pool->rows[slot].level == level) {
            return &pool->rows[slot];
        }
    }
    return NULL;
}

static const BrowseRows *cached_rows(const BrowseScreen *browse)
{
    const BrowsePool *pool = browse->pool;
    for (int slot = 0; slot < BROWSE_ROW_CACHES; slot++) {
        if (pool->rows[slot].level == browse->level) {
            return &pool->rows[slot];
        }
    }
    return NULL;
}

static BrowseRows *rows_to_fill(BrowseScreen *browse)
{
    BrowsePool *pool = browse->pool;
    BrowseRows *rows = rows_of_level(pool, browse->level);
    if (rows == NULL) {
        rows = &pool->rows[0];
        for (int slot = 1; slot < BROWSE_ROW_CACHES; slot++) {
            if (pool->rows[slot].used < rows->used) {
                rows = &pool->rows[slot];
            }
        }
    }
    rows->used = ++pool->row_uses;
    return rows;
}

static void forget_rows(BrowseScreen *browse)
{
    BrowseRows *rows = rows_of_level(browse->pool, browse->level);
    if (rows != NULL) {
        rows->level = NO_LEVEL;
    }
}

static bool rows_current(const BrowseScreen *browse)
{
    const BrowseRows *rows = cached_rows(browse);
    return rows != NULL && rows->first == browse->list.top;
}

static uint32_t source_position(const BrowseScreen *browse, int index)
{
    return browse->favorites_only ? browse->pool->favorite_albums[index] : (uint32_t)index;
}

static bool item_at(const BrowseScreen *browse, int index, BrowseItem *item)
{
    if (browse->shelf_root) {
        return spoken_shelf_item(browse->pool->config.library, &browse->pool->shelf_cursor,
                                 (uint32_t)index, item);
    }
    return browse_item(browse->pool->config.library, browse->query, source_position(browse, index),
                       item);
}

static bool is_favorite(const BrowseScreen *browse, BrowseItem item)
{
    const FavoritesStore *favorites = browse->pool->config.favorites;
    return item.kind == BROWSE_ITEM_ALBUM && favorites != NULL &&
           favorites_store_contains_album(favorites, item.index);
}

static void mark_favorite(char *detail)
{
    char year[BROWSE_DETAIL_CAPACITY];
    StringBuilder copy = string_builder_make(year, sizeof year);
    string_builder_append(&copy, detail);
    StringBuilder builder = string_builder_make(detail, BROWSE_DETAIL_CAPACITY);
    string_builder_append(&builder, STAR);
    if (year[0] != '\0') {
        string_builder_append_char(&builder, ' ');
        string_builder_append(&builder, year);
    }
}

static void describe_spoken(const BrowseScreen *browse, BrowseItem item, char *detail)
{
    const SpokenStore *spoken = browse->pool->config.spoken;
    if (browse->shelf == SPOKEN_NONE || spoken == NULL) {
        return;
    }
    SpokenRow row = {
        .shelf = browse->shelf, .item = item, .detail = detail, .capacity = BROWSE_DETAIL_CAPACITY};
    spoken_store_describe(spoken, row);
}

static void load_row(BrowseScreen *browse, BrowseRows *rows, int row, int index)
{
    BrowseText text = {rows->labels[row], BROWSE_LABEL_CAPACITY, rows->details[row],
                       BROWSE_DETAIL_CAPACITY};
    BrowseItem item;
    rows->details[row][0] = '\0';
    rows->opens[row] = false;
    if (!item_at(browse, index, &item) ||
        !browse_item_text(browse->pool->config.library, browse->query, item, text)) {
        rows->labels[row][0] = '\0';
        rows->details[row][0] = '\0';
        return;
    }
    rows->opens[row] = item.kind != BROWSE_ITEM_TRACK;
    if (is_favorite(browse, item)) {
        mark_favorite(rows->details[row]);
    }
    describe_spoken(browse, item, rows->details[row]);
}

static void refresh_rows(BrowseScreen *browse)
{
    if (rows_current(browse)) {
        return;
    }
    BrowseRows *rows = rows_to_fill(browse);
    rows->level = NO_LEVEL;
    int top = browse->list.top;
    int count = min_of(min_of(browse->list.visible_rows, BROWSE_ROWS), (int)browse->count - top);
    for (int row = 0; row < count; row++) {
        load_row(browse, rows, row, top + row);
    }
    rows->level = browse->level;
    rows->first = top;
    rows->count = count;
}

static const char *cached_text(const BrowseScreen *browse, int index, bool detail)
{
    const BrowseRows *rows = cached_rows(browse);
    int row = rows != NULL ? index - rows->first : -1;
    if (rows == NULL || row < 0 || row >= rows->count) {
        return "";
    }
    return detail ? rows->details[row] : rows->labels[row];
}

static int row_count(const void *context)
{
    const BrowseScreen *browse = context;
    return (int)browse->count;
}

static const char *row_label(const void *context, int index)
{
    return cached_text(context, index, false);
}

static const char *row_detail(const void *context, int index)
{
    const char *detail = cached_text(context, index, true);
    return detail[0] != '\0' ? detail : NULL;
}

static bool row_opens(const void *context, int index)
{
    const BrowseRows *rows = cached_rows(context);
    int row = rows != NULL ? index - rows->first : -1;
    return rows != NULL && row >= 0 && row < rows->count && rows->opens[row];
}

static int row_section(const void *context, int index)
{
    const BrowseScreen *browse = context;
    return browse->pool->config.section(browse, index);
}

static ListSections row_sections(const void *context, int index)
{
    const BrowseScreen *browse = context;
    return browse->pool->config.sections(browse, index);
}

static bool is_loaded_track(const BrowseScreen *browse, int index)
{
    BrowseItem item;
    PlayerStatus status = player_status(browse->pool->config.player);
    return status.has_track && status.state != PLAYER_IDLE && item_at(browse, index, &item) &&
           item.kind == BROWSE_ITEM_TRACK && item.index == status.track;
}

static ScreenAction start_list(BrowseScreen *browse, BrowseQuery source, uint32_t position)
{
    BrowsePoolConfig *config = &browse->pool->config;
    BrowseQuery played = browse_play_source(config->library, source, &position);
    PlayerListStart start = {.source = played,
                             .position = position,
                             .seed = hal_clock_microseconds(),
                             .in_order = browse->shelf != SPOKEN_NONE};
    if (!player_play_from(config->player, start)) {
        return screen_action_none();
    }
    return screen_action_push(config->now_playing);
}

static ScreenAction play_from(BrowseScreen *browse, int index)
{
    if (is_loaded_track(browse, index)) {
        if (browse->shelf_root &&
            player_status(browse->pool->config.player).state == PLAYER_PAUSED) {
            player_toggle_pause(browse->pool->config.player);
        }
        return screen_action_push(browse->pool->config.now_playing);
    }
    if (browse->shelf_root) {
        BrowseItem item;
        if (!item_at(browse, index, &item)) {
            return screen_action_none();
        }
        BrowseQuery source = browse_root_query(BROWSE_SINGLE_TRACK);
        source.subject = item.index;
        return start_list(browse, source, 0);
    }
    return start_list(browse, browse->query, (uint32_t)index);
}

static ScreenAction open_child(BrowseScreen *browse, BrowseItem item)
{
    BrowseQuery child;
    if (!browse_child(browse->query, item, &child)) {
        return screen_action_none();
    }
    Screen *next = browse_pool_open(browse->pool, browse->level + 1, child);
    if (next == NULL) {
        return screen_action_none();
    }
    browse->pool->screens[browse->level + 1].shelf = browse->shelf;
    return screen_action_push(next);
}

static bool playing_folder(const BrowseScreen *browse, uint16_t folder)
{
    PlayerStatus status = player_status(browse->pool->config.player);
    return status.has_track && status.state != PLAYER_IDLE &&
           spoken_folder_holds(browse->pool->config.library, folder, status.track);
}

static ScreenAction resume_book(BrowseScreen *browse, BrowseItem item)
{
    const BrowsePoolConfig *config = &browse->pool->config;
    LibraryFolder folder;
    BrowseQuery child;
    if (config->spoken == NULL || !library_read_folder(config->library, item.index, &folder) ||
        folder.track_count == 0 || !browse_child(browse->query, item, &child)) {
        return open_child(browse, item);
    }
    if (playing_folder(browse, item.index)) {
        if (player_status(config->player).state == PLAYER_PAUSED) {
            player_toggle_pause(config->player);
        }
        return screen_action_push(config->now_playing);
    }
    uint16_t chapter = spoken_store_resume_chapter(config->spoken, item.index);
    return start_list(browse, child, (uint32_t)folder.subfolder_count + chapter);
}

static bool resumes_books(const BrowseScreen *browse, BrowseItem item)
{
    return browse->shelf == SPOKEN_AUDIOBOOK && item.kind == BROWSE_ITEM_FOLDER;
}

static ScreenAction select_row(void *context, int index)
{
    BrowseScreen *browse = context;
    BrowseItem item;
    if (!item_at(browse, index, &item)) {
        return screen_action_none();
    }
    if (item.kind == BROWSE_ITEM_TRACK) {
        return play_from(browse, index);
    }
    if (resumes_books(browse, item)) {
        return resume_book(browse, item);
    }
    return open_child(browse, item);
}

static void keep_audio_fed(void *context)
{
    (void)context;
    hal_audio_service();
}

static void refresh_favorites(BrowsePool *pool)
{
    const FavoritesStore *store = pool->config.favorites;
    if (pool->favorites_collected && pool->favorites_revision == store->revision) {
        return;
    }
    FavoritesCollector collector = {.albums = pool->favorite_albums,
                                    .capacity = FAVORITES_CAPACITY,
                                    .between_albums = keep_audio_fed};
    pool->favorite_count = favorites_collect(&store->favorites, pool->config.library, collector);
    pool->favorites_revision = store->revision;
    pool->favorites_collected = true;
}

static void collect_favorites(BrowseScreen *browse)
{
    refresh_favorites(browse->pool);
    browse->count = browse->pool->favorite_count;
    list_view_select(&browse->list, browse->list.selected, browse->now_ms);
}

static ScreenAction toggle_favorite(void *context, int index)
{
    BrowseScreen *browse = context;
    BrowsePoolConfig *config = &browse->pool->config;
    BrowseItem item;
    if (config->favorites == NULL || !item_at(browse, index, &item) ||
        item.kind != BROWSE_ITEM_ALBUM) {
        return screen_action_none();
    }
    FavoriteResult result = favorites_store_toggle_album(config->favorites, item.index);
    if (config->toast != NULL) {
        toast_show(config->toast, favorites_store_message(result), browse->now_ms);
    }
    forget_rows(browse);
    if (browse->favorites_only) {
        collect_favorites(browse);
    }
    return screen_action_none();
}

static StringId up_next_message(PlayerUpNextResult result, bool next)
{
    switch (result) {
    case PLAYER_UP_NEXT_ADDED:
        return next ? STR_PLAYING_NEXT : STR_ADDED_TO_UP_NEXT;
    case PLAYER_UP_NEXT_STARTED:
        return STR_PLAYING;
    case PLAYER_UP_NEXT_FULL:
        return STR_UP_NEXT_FULL;
    case PLAYER_UP_NEXT_SAVE_FAILED:
        return STR_QUEUE_SAVE_FAILED;
    case PLAYER_UP_NEXT_CHANGED:
        return STR_QUEUE_CHANGED;
    case PLAYER_UP_NEXT_WORKING:
        return STR_PREPARING_STORAGE;
    case PLAYER_UP_NEXT_FAILED:
        break;
    }
    return STR_SONG_FAILED;
}

static ScreenAction queue_song(BrowsePool *pool, bool next)
{
    SongActions *actions = &pool->song_actions;
    Player *player = pool->config.player;
    PlayerUpNextResult result = next ? player_play_next(player, actions->track)
                                     : player_add_to_up_next(player, actions->track);
    if (pool->config.toast != NULL) {
        toast_show(pool->config.toast, i18n_text(up_next_message(result, next)), actions->now_ms);
    }
    return screen_action_pop();
}

static ScreenAction play_song_next(void *context)
{
    return queue_song(context, true);
}

static ScreenAction add_song_to_up_next(void *context)
{
    return queue_song(context, false);
}

static ScreenAction add_song_to_favorites(void *context)
{
    BrowsePool *pool = context;
    SongActions *actions = &pool->song_actions;
    FavoritePlaylistResult result = favorite_playlist_add(pool->config.library, actions->track);
    if (pool->config.toast != NULL) {
        toast_show(pool->config.toast, favorite_playlist_message(result), actions->now_ms);
    }
    return screen_action_pop();
}

static ScreenAction open_track_info(void *context)
{
    BrowsePool *pool = context;
    Screen *screen = pool->config.info(pool->config.tools_context, pool->song_actions.track);
    return screen != NULL ? screen_action_push(screen) : screen_action_none();
}

static ScreenAction open_track_tags(void *context)
{
    BrowsePool *pool = context;
    Screen *screen = pool->config.tags(pool->config.tools_context, pool->song_actions.track);
    return screen != NULL ? screen_action_push(screen) : screen_action_none();
}

Screen *browse_pool_song_actions(BrowsePool *pool, uint16_t track, const char *name,
                                 uint32_t now_ms)
{
    SongActions *actions = &pool->song_actions;
    actions->track = track;
    actions->now_ms = now_ms;
    StringBuilder title = string_builder_make(actions->title, sizeof actions->title);
    string_builder_append(&title, name);
    actions->items[0] =
        (MenuItem){.label = i18n_text(STR_PLAY_NEXT), .select = play_song_next, .context = pool};
    actions->items[1] = (MenuItem){
        .label = i18n_text(STR_ADD_TO_UP_NEXT), .select = add_song_to_up_next, .context = pool};
    actions->items[2] = (MenuItem){.label = i18n_text(STR_ADD_TO_FAVORITE_PLAYLIST),
                                   .select = add_song_to_favorites,
                                   .context = pool};
    int count = 3;
    if (pool->config.info != NULL) {
        actions->items[count++] = (MenuItem){.label = i18n_text(STR_TRACK_INFO),
                                             .select = open_track_info,
                                             .context = pool,
                                             .opens = true};
    }
    if (pool->config.tags != NULL) {
        actions->items[count++] = (MenuItem){.label = i18n_text(STR_PERSONAL_TAGS),
                                             .select = open_track_tags,
                                             .context = pool,
                                             .opens = true};
    }
    MenuConfig config = {.title = actions->title,
                         .items = actions->items,
                         .item_count = count,
                         .theme = pool->config.theme};
    return menu_screen_init(&actions->menu, config);
}

static ScreenAction open_song_actions(BrowseScreen *browse, int index, uint16_t track)
{
    return screen_action_push(
        browse_pool_song_actions(browse->pool, track, row_label(browse, index), browse->now_ms));
}

static ScreenAction play_folder(BrowseScreen *browse, BrowseItem item)
{
    BrowsePoolConfig *config = &browse->pool->config;
    BrowseQuery tree;
    if (!browse_child(browse->query, item, &tree)) {
        return screen_action_none();
    }
    tree.kind = BROWSE_FOLDER_TREE;
    uint32_t count = browse_count(config->library, tree);
    if (count == 0) {
        if (config->toast != NULL) {
            toast_show(config->toast, i18n_text(STR_NO_SONGS_IN_FOLDER), browse->now_ms);
        }
        return screen_action_none();
    }
    bool shuffled = player_status(config->player).shuffle == SHUFFLE_SONGS;
    return start_list(browse, tree, shuffled ? hal_clock_microseconds() % count : 0);
}

static ScreenAction hold_row(void *context, int index)
{
    BrowseScreen *browse = context;
    BrowseItem item;
    if (!item_at(browse, index, &item)) {
        return screen_action_none();
    }
    if (browse->pool->config.player != NULL && item.kind == BROWSE_ITEM_TRACK) {
        return open_song_actions(browse, index, item.index);
    }
    if (browse->shelf != SPOKEN_NONE && item.kind == BROWSE_ITEM_FOLDER) {
        return open_child(browse, item);
    }
    if (browse->pool->config.player != NULL && item.kind == BROWSE_ITEM_FOLDER) {
        return play_folder(browse, item);
    }
    if (item.kind == BROWSE_ITEM_GROUP && browse->pool->config.play_group != NULL) {
        Screen *screen =
            browse->pool->config.play_group(browse->pool->config.tools_context, browse);
        return screen != NULL ? screen_action_push(screen) : screen_action_none();
    }
    return toggle_favorite(context, index);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    BrowseScreen *browse = context;
    browse->now_ms = now_ms;
    if (event.key == KEY_MENU && event.gesture == KEY_GESTURE_LONG_PRESS &&
        browse->pool->config.options != NULL) {
        Screen *screen = browse->pool->config.options(browse->pool->config.tools_context, browse);
        return screen != NULL ? screen_action_push(screen) : screen_action_none();
    }
    ListSelection selection = {.select = select_row, .hold = hold_row, .context = browse};
    return list_navigation_handle(&browse->list, event, now_ms, selection);
}

static void enter(void *context, uint32_t now_ms)
{
    BrowseScreen *browse = context;
    forget_rows(browse);
    browse->now_ms = now_ms;
    if (browse->favorites_only) {
        collect_favorites(browse);
    }
    list_view_fit(&browse->list);
    browse->screen.fast_list = browse->count > (uint32_t)browse->list.visible_rows;
    list_view_restart_marquee(&browse->list, now_ms);
    refresh_rows(browse);
}

static void update(void *context, uint32_t now_ms)
{
    BrowseScreen *browse = context;
    browse->now_ms = now_ms;
    refresh_rows(browse);
}

static void draw_empty(Canvas *canvas, const Theme *theme, Rect content, const char *message)
{
    canvas_fill_rect(canvas, content, theme->colors.background);
    message_draw(canvas, theme, content, message);
}

static const char *empty_message(const BrowseScreen *browse)
{
    if (browse->favorites_only) {
        return i18n_text(STR_NO_FAVORITES_HINT);
    }
    if (browse->shelf_root) {
        return i18n_text(browse->shelf == SPOKEN_PODCAST ? STR_NO_SHOWS_HINT : STR_NO_BOOKS_HINT);
    }
    return i18n_text(STR_NOTHING_HERE);
}

static void draw(void *context, Canvas *canvas)
{
    const BrowseScreen *browse = context;
    const Theme *theme = browse->pool->config.theme;
    Rect content = theme_content_area(theme);
    header_draw(canvas, theme, browse->title);
    if (browse->count == 0) {
        draw_empty(canvas, theme, content, empty_message(browse));
        return;
    }
    list_view_draw(&browse->list, canvas, content, browse->now_ms);
}

void browse_pool_init(BrowsePool *pool, BrowsePoolConfig config)
{
    pool->config = config;
    pool->row_uses = 0;
    pool->favorites_collected = false;
    for (int slot = 0; slot < BROWSE_ROW_CACHES; slot++) {
        pool->rows[slot] = (BrowseRows){.level = NO_LEVEL};
    }
}

Screen *browse_pool_open(BrowsePool *pool, int level, BrowseQuery query)
{
    if (level < 0 || level >= BROWSE_DEPTH) {
        return NULL;
    }
    BrowseScreen *browse = &pool->screens[level];
    browse->pool = pool;
    browse->level = level;
    browse->query = query;
    browse->favorites_only = false;
    browse->shelf = SPOKEN_NONE;
    browse->shelf_root = false;
    browse->count = browse_count(pool->config.library, query);
    browse_title(pool->config.library, query, browse->title, sizeof browse->title);
    browse->model = (ListModel){.context = browse,
                                .count = row_count,
                                .label = row_label,
                                .detail = row_detail,
                                .opens = row_opens,
                                .section = pool->config.section != NULL ? row_section : NULL,
                                .sections = pool->config.sections != NULL ? row_sections : NULL};
    list_view_init(&browse->list, &browse->model, pool->config.theme, 0);
    browse->screen = (Screen){
        .title = browse->title,
        .context = browse,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &browse->screen;
}

Screen *browse_pool_open_favorites(BrowsePool *pool, int level)
{
    if (pool->config.favorites == NULL) {
        return NULL;
    }
    Screen *screen = browse_pool_open(pool, level, browse_root_query(BROWSE_ALBUMS));
    if (screen == NULL) {
        return NULL;
    }
    BrowseScreen *browse = &pool->screens[level];
    StringBuilder title = string_builder_make(browse->title, sizeof browse->title);
    string_builder_append(&title, i18n_text(STR_FAVORITES));
    browse->favorites_only = true;
    collect_favorites(browse);
    return screen;
}

static BrowseQuery shelf_query(const BrowsePool *pool, SpokenKind shelf, bool *found)
{
    uint16_t folder = 0;
    BrowseQuery query = browse_root_query(BROWSE_FOLDER);
    *found = spoken_shelf_folder(pool->config.library, shelf, &folder);
    if (*found) {
        BrowseItem item = {.kind = BROWSE_ITEM_FOLDER, .index = folder};
        browse_child(query, item, &query);
    }
    return query;
}

Screen *browse_pool_open_shelf(BrowsePool *pool, int level, SpokenKind shelf)
{
    bool found = false;
    Screen *screen = browse_pool_open(pool, level, shelf_query(pool, shelf, &found));
    if (screen == NULL) {
        return NULL;
    }
    BrowseScreen *browse = &pool->screens[level];
    StringBuilder title = string_builder_make(browse->title, sizeof browse->title);
    string_builder_append(&title,
                          i18n_text(shelf == SPOKEN_PODCAST ? STR_PODCASTS : STR_AUDIOBOOKS));
    browse->shelf = shelf;
    browse->shelf_root = true;
    spoken_shelf_init(&pool->shelf_cursor, shelf, keep_audio_fed, NULL);
    browse->count = spoken_shelf_count(pool->config.library, &pool->shelf_cursor);
    return screen;
}
