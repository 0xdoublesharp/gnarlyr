#include <stdbool.h>
#include <stddef.h>

#include "browse_screen.h"
#include "core/i18n.h"
#include "core/string_builder.h"

enum {
    ACTION_ALWAYS,
    ACTION_INFO,
    ACTION_TAGS,
    ACTION_MANAGE
};

typedef struct SongAction {
    StringId label;
    ScreenAction (*select)(void *context);
    uint8_t needs;
    bool opens;
    ListRowRole role;
} SongAction;

static const SongAction SONG_ACTIONS[] = {
    {STR_PLAY_NEXT, browse_song_play_next, ACTION_ALWAYS, false, LIST_ROW_PLAIN},
    {STR_ADD_TO_UP_NEXT, browse_song_add_to_up_next, ACTION_ALWAYS, false, LIST_ROW_PLAIN},
    {STR_ADD_TO_FAVORITE_PLAYLIST, browse_song_add_to_favorites, ACTION_MANAGE, false,
     LIST_ROW_PLAIN},
    {STR_TRACK_INFO, browse_song_open_info, ACTION_INFO, true, LIST_ROW_PLAIN},
    {STR_PERSONAL_TAGS, browse_song_open_tags, ACTION_TAGS, true, LIST_ROW_PLAIN},
    {STR_ADD_TO_TAPE, browse_song_add_to_tape, ACTION_MANAGE, true, LIST_ROW_PLAIN},
    {STR_HIDE_TRACK, browse_song_manage, ACTION_MANAGE, false, LIST_ROW_PLAIN},
    {STR_DELETE_FILE_ASK, browse_song_manage, ACTION_MANAGE, false, LIST_ROW_DANGER_DIVIDED},
};

static bool action_offered(const BrowsePool *pool, uint8_t needs)
{
    const bool offered[] = {true, pool->config.info != NULL, pool->config.tags != NULL,
                            pool->config.manage != NULL};
    return offered[needs];
}

Screen *browse_pool_song_actions(BrowsePool *pool, uint16_t track, const char *name,
                                 uint32_t now_ms)
{
    SongActions *actions = &pool->song_actions;
    actions->track = track;
    actions->now_ms = now_ms;
    StringBuilder title = string_builder_make(actions->title, sizeof actions->title);
    string_builder_append(&title, name);
    int count = 0;
    for (size_t index = 0; index < sizeof SONG_ACTIONS / sizeof SONG_ACTIONS[0]; index++) {
        const SongAction *action = &SONG_ACTIONS[index];
        if (action_offered(pool, action->needs)) {
            actions->items[count++] = (MenuItem){.label = i18n_text(action->label),
                                                 .select = action->select,
                                                 .context = pool,
                                                 .opens = action->opens,
                                                 .role = action->role};
        }
    }
    MenuConfig config = {.title = actions->title,
                         .items = actions->items,
                         .item_count = count,
                         .theme = pool->config.theme};
    return menu_screen_init(&actions->menu, config);
}
