#pragma once

#include "discovery_text.h"
#include "library/library_view.h"
#include "personal_tags.h"
#include "player.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/wire_motion.h"

typedef enum DiscoveryRequest {
    DISCOVERY_REQUEST_NONE,
    DISCOVERY_REQUEST_PLAY,
    DISCOVERY_REQUEST_CANCEL_PLAY
} DiscoveryRequest;

typedef enum DiscoveryMode {
    DISCOVERY_RESULTS,
    DISCOVERY_OPTIONS,
    DISCOVERY_SORT,
    DISCOVERY_FILTERS,
    DISCOVERY_PICKER,
    DISCOVERY_EDITOR,
    DISCOVERY_INFO,
    DISCOVERY_TAGS,
    DISCOVERY_TAG_ACTIONS,
    DISCOVERY_TAG_DELETE,
    DISCOVERY_TAG_FILTER,
    DISCOVERY_ACTIONS,
    DISCOVERY_NOTICE
} DiscoveryMode;

typedef struct DiscoveryScreenConfig {
    const Library *library;
    Player *player;
    const Theme *theme;
    Screen *now_playing;
    PersonalTags *tags;
    BuilderScratch scratch;
    LibraryViewAcceptance (*scope_accept)(void *context, uint16_t track,
                                          const LibraryTrack *record);
    void *scope_context;
    const char *scope_title;
    WireMotion *motion;
} DiscoveryScreenConfig;

typedef struct DiscoveryScreen {
    Screen screen;
    DiscoveryScreenConfig config;
    ListModel model;
    ListView list;
    LibraryViewConfig query;
    DiscoveryText editor;
    Screen *destination;
    DiscoveryRequest request;
    uint32_t now_ms;
    uint32_t required_tags;
    uint32_t tag_binding;
    uint16_t play_position;
    uint16_t selected_track;
    uint16_t info_track;
    uint8_t mode;
    uint8_t previous_mode;
    uint8_t edit_kind;
    uint8_t picker_kind;
    uint8_t tag;
    BrowseQuery scope_query;
    struct BrowseScreen *scope_browse;
    char scope_title_text[96];
    bool scope_favorites;
    bool auto_play;
    bool initialized;
    bool info_external;
    bool editor_external;
} DiscoveryScreen;

Screen *discovery_screen_init(DiscoveryScreen *screen, DiscoveryScreenConfig config);
Screen *discovery_screen_open(DiscoveryScreen *screen, BrowseQuery source, bool edit_search);
Screen *discovery_screen_options(DiscoveryScreen *screen, BrowseQuery source);
Screen *discovery_screen_info(DiscoveryScreen *screen, uint16_t track);
Screen *discovery_screen_tags(DiscoveryScreen *screen, uint16_t track);
TrackOrderSource discovery_screen_order(DiscoveryScreen *screen);
void discovery_screen_playback_done(DiscoveryScreen *screen, bool success);
void discovery_screen_cancel(DiscoveryScreen *screen);

LibraryViewState discovery_screen_state(const DiscoveryScreen *screen);
