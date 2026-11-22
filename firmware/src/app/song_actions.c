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
} SongAction;

static const SongAction SONG_ACTIONS[] = {
    {STR_PLAY_NEXT, browse_song_play_next, ACTION_ALWAYS, false},
    {STR_ADD_TO_UP_NEXT, browse_song_add_to_up_next, ACTION_ALWAYS, false},
    {STR_ADD_TO_FAVORITE_PLAYLIST, browse_song_add_to_favorites, ACTION_MANAGE, false},
    {STR_TRACK_INFO, browse_song_open_info, ACTION_INFO, true},
    {STR_PERSONAL_TAGS, browse_song_open_tags, ACTION_TAGS, true},
    {STR_ADD_TO_TAPE, browse_song_add_to_tape, ACTION_MANAGE, true},
    {STR_HIDE_TRACK, browse_song_manage, ACTION_MANAGE, false},
    {STR_DELETE_FILE, browse_song_manage, ACTION_MANAGE, false},
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
    actions->confirming = false;
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
                                                 .opens = action->opens};
        }
    }
    MenuConfig config = {.title = actions->title,
                         .items = actions->items,
                         .item_count = count,
                         .theme = pool->config.theme};
    return menu_screen_init(&actions->menu, config);
}
