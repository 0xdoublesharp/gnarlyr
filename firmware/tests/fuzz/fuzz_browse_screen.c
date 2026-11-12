#include <stddef.h>
#include <stdint.h>

#include "browse_screen.h"
#include "core/key_tracker.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "fuzz_source.h"
#include "gfx/canvas.h"
#include "hal/display.h"

enum {
    ACTION_LIMIT = 96,
    ROOT_COUNT = BROWSE_ALL_SONGS + 1
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_browse(BrowseScreen *browse)
{
    fuzz_require(browse->level >= 0 && browse->level < BROWSE_DEPTH);
    fuzz_require(browse->screen.title != NULL);
    fuzz_require(browse->list.selected >= 0);
    fuzz_require(browse->model.count(browse->model.context) == (int)browse->count);
    if (browse->count > 0) {
        fuzz_require((uint32_t)browse->list.selected < browse->count);
        int selected = browse->list.selected;
        fuzz_require(browse->model.label(browse->model.context, selected) != NULL);
        browse->model.detail(browse->model.context, selected);
        browse->model.opens(browse->model.context, selected);
        if (browse->model.section != NULL) {
            int section = browse->model.section(browse->model.context, selected);
            fuzz_require(section >= LIST_SECTION_BEFORE_LETTERS &&
                         section <= LIST_SECTION_AFTER_LETTERS);
        }
    }
}

static void draw_screen(Screen *screen, const Theme *theme)
{
    if (screen->draw == NULL) {
        return;
    }
    static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    screen->draw(screen->context, &canvas);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) {
        return 0;
    }
    static const Key KEYS[] = {KEY_VOLUME_UP, KEY_VOLUME_DOWN, KEY_PLAY, KEY_MENU, KEY_BACK};
    static const KeyGesture GESTURES[] = {KEY_GESTURE_PRESS, KEY_GESTURE_LONG_PRESS,
                                          KEY_GESTURE_REPEAT, KEY_GESTURE_DOUBLE_PRESS,
                                          KEY_GESTURE_TRIPLE_PRESS};
    FixtureLibrary fixture;
    BrowsePool pool;
    Player player;
    Screen now_playing = {.title = "Now Playing"};
    fake_hal_reset();
    fixture_library_open(&fixture);
    player_init(&player, &fixture.library);
    browse_pool_init(&pool, (BrowsePoolConfig){.library = &fixture.library,
                                               .theme = &BOARD_THEME,
                                               .player = &player,
                                               .now_playing = &now_playing});
    Screen *active =
        browse_pool_open(&pool, 0, browse_root_query((BrowseKind)(data[0] % ROOT_COUNT)));
    fuzz_require(active != NULL);
    active->enter(active->context, 0);
    draw_screen(active, &BOARD_THEME);
    size_t limit = size < ACTION_LIMIT ? size : ACTION_LIMIT;
    for (size_t index = 1; index < limit; index++) {
        uint8_t value = data[index];
        if ((value & 7u) == 0) {
            active->update(active->context, (uint32_t)index * 271u);
        } else {
            ScreenAction action = active->handle_key(
                active->context,
                (KeyEvent){.key = KEYS[value % 5], .gesture = GESTURES[(value / 5) % 5]},
                (uint32_t)index * 271u);
            fuzz_require(action.type <= SCREEN_ACTION_REPLACE);
            if ((action.type == SCREEN_ACTION_PUSH || action.type == SCREEN_ACTION_REPLACE) &&
                action.target != NULL && action.target != &now_playing) {
                active = action.target;
                if (active->enter != NULL) {
                    active->enter(active->context, (uint32_t)index * 271u);
                }
            } else if (action.type == SCREEN_ACTION_POP ||
                       action.type == SCREEN_ACTION_POP_TO_ROOT || action.target == &now_playing) {
                active = &pool.screens[0].screen;
            }
        }
        if (active->context >= (void *)&pool.screens[0] &&
            active->context <= (void *)&pool.screens[BROWSE_DEPTH - 1]) {
            check_browse(active->context);
        }
        if ((value & 7u) == 0) {
            draw_screen(active, &BOARD_THEME);
        }
        player_update(&player, (uint32_t)index * 271u);
    }
    return 0;
}
