#include <stddef.h>
#include <stdint.h>

#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "fuzz_source.h"
#include "player_storage.h"
#include "screens/cover_flow_screen.h"

enum {
    ACTION_LIMIT = 96,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static KeyEvent event_for(uint8_t value)
{
    return (KeyEvent){.key = (Key)(value % KEY_COUNT),
                      .gesture = (KeyGesture)((value / KEY_COUNT) % 5),
                      .repeats = value};
}

static void draw(Screen *screen, const Theme *theme)
{
    static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    screen->draw(screen->context, &canvas);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fake_hal_reset();
    static FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    uint32_t covers_size = 0;
    const uint8_t *covers_bytes = fixture_covers_bytes(&covers_size);
    uint8_t first = size > 0 ? data[0] : 0;
    if ((first & 1u) == 0) {
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/covers.bin", covers_bytes, covers_size);
    }
    fake_hal_add_file(PLAYER_SESSION_PATH, NULL, 4096);
    fake_hal_add_file(FAVORITES_STORE_PATH, NULL, 4096);
    Theme theme = BOARD_THEME;
    default_theme_set_orientation(&theme, (Orientation)(first % ORIENTATION_COUNT));
    default_theme_set_text_size(&theme, (TextSize)((first / 2u) % TEXT_SIZE_COUNT));
    default_theme_set_appearance(&theme, (AppearanceTheme)((first / 4u) % APPEARANCE_THEME_COUNT));
    CoverCache covers;
    cover_cache_open(&covers, library);
    FavoritesStore favorites;
    favorites_store_load(&favorites, library);
    Player player;
    player_init(&player, library);
    if ((first & 2u) != 0) {
        player_play_list(&player, browse_root_query(BROWSE_ALL_SONGS), 0, first);
    }
    Toast toast = {0};
    Screen now_playing = {.title = "Now playing"};
    BrowsePool browse;
    browse_pool_init(&browse, (BrowsePoolConfig){.library = library,
                                                 .theme = &theme,
                                                 .player = &player,
                                                 .now_playing = &now_playing,
                                                 .favorites = &favorites,
                                                 .toast = &toast});
    CoverFlowScreen flow;
    Screen *screen = cover_flow_screen_init(&flow, (CoverFlowConfig){.library = library,
                                                                     .theme = &theme,
                                                                     .covers = &covers,
                                                                     .favorites = &favorites,
                                                                     .toast = &toast,
                                                                     .browse = &browse,
                                                                     .player = &player});
    screen->enter(screen->context, 0);
    draw(screen, &theme);
    uint16_t album = 0;
    uint32_t position = 0;
    cover_flow_order_album(library, flow.order, first, &album);
    cover_flow_order_find(library, flow.order, album, &position);
    size_t limit = size < ACTION_LIMIT ? size : ACTION_LIMIT;
    for (size_t index = 0; index < limit; index++) {
        uint8_t value = data[index];
        uint32_t now_ms = (uint32_t)(index + 1) * (257u + value);
        if ((value & 31u) == 0) {
            flow.positioned = false;
            screen->enter(screen->context, now_ms);
        }
        ScreenAction action = screen->handle_key(screen->context, event_for(value), now_ms);
        fuzz_require(action.type <= SCREEN_ACTION_REPLACE);
        screen->update(screen->context, now_ms + (value & 15u));
        fuzz_require(flow.count >= 0 && flow.target >= 0 &&
                     (flow.count == 0 || flow.target < flow.count));
        if ((value & 3u) == 0) {
            draw(screen, &theme);
        }
    }
    player_release_output(&player);
    return 0;
}
