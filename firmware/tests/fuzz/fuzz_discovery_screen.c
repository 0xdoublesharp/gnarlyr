#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/key_tracker.h"
#include "default_theme.h"
#include "discovery_screen.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "fuzz_source.h"
#include "gfx/canvas.h"
#include "hal/display.h"

enum {
    ACTION_LIMIT = 96,
    ROOT_COUNT = BROWSE_ALL_SONGS + 1,
    CONTROL_BYTES = 4
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct Scratch {
    uint8_t bytes[LIBRARY_VIEW_WORK_BYTES];
    bool refuse_reads;
    bool refuse_writes;
} Scratch;

static bool scratch_read(void *context, uint32_t offset, void *bytes, uint32_t count)
{
    Scratch *scratch = context;
    if (scratch->refuse_reads || offset > sizeof scratch->bytes ||
        count > sizeof scratch->bytes - offset) {
        return false;
    }
    memcpy(bytes, scratch->bytes + offset, count);
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t count)
{
    Scratch *scratch = context;
    if (scratch->refuse_writes || offset > sizeof scratch->bytes ||
        count > sizeof scratch->bytes - offset) {
        return false;
    }
    memcpy(scratch->bytes + offset, bytes, count);
    return true;
}

static void check_screen(DiscoveryScreen *screen, const FixtureLibrary *fixture)
{
    LibraryViewState state = discovery_screen_state(screen);
    fuzz_require(state >= LIBRARY_VIEW_IDLE && state <= LIBRARY_VIEW_CHANGED);
    fuzz_require(screen->mode <= DISCOVERY_NOTICE);
    fuzz_require(screen->request <= DISCOVERY_REQUEST_CANCEL_PLAY);
    fuzz_require(screen->screen.title != NULL);
    fuzz_require(screen->list.selected >= 0);
    TrackOrderSource order = discovery_screen_order(screen);
    fuzz_require(order.count <= library_count(&fixture->library, LIBRARY_TRACKS));
    if (screen->model.count != NULL) {
        int count = screen->model.count(screen->model.context);
        fuzz_require(count >= 0);
        if (count > 0) {
            fuzz_require(screen->list.selected < count);
        }
    }
}

static void apply_action(DiscoveryScreen *screen, Scratch *scratch, uint8_t value, uint32_t now_ms)
{
    static const Key KEYS[] = {KEY_VOLUME_UP, KEY_VOLUME_DOWN, KEY_PLAY, KEY_MENU, KEY_BACK};
    static const KeyGesture GESTURES[] = {KEY_GESTURE_PRESS, KEY_GESTURE_LONG_PRESS,
                                          KEY_GESTURE_REPEAT, KEY_GESTURE_DOUBLE_PRESS,
                                          KEY_GESTURE_TRIPLE_PRESS};
    switch (value >> 5) {
    case 0:
    case 1:
    case 2:
    case 3:
        screen->screen.handle_key(
            screen, (KeyEvent){.key = KEYS[value % 5], .gesture = GESTURES[(value / 5) % 5]},
            now_ms);
        break;
    case 4:
        screen->screen.update(screen, now_ms);
        break;
    case 5:
        discovery_screen_playback_done(screen, (value & 1u) != 0);
        break;
    case 6:
        scratch->refuse_reads = (value & 1u) != 0;
        scratch->refuse_writes = (value & 2u) != 0;
        break;
    default:
        discovery_screen_cancel(screen);
        break;
    }
}

static void draw_screen(DiscoveryScreen *screen, const Theme *theme)
{
    static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    screen->screen.draw(screen, &canvas);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) {
        return 0;
    }
    FixtureLibrary fixture;
    DiscoveryScreen screen;
    Player player;
    WireMotion motion = {0};
    Scratch scratch = {0};
    Theme theme = BOARD_THEME;
    fake_hal_reset();
    if (size > 2 && (data[2] & 2u) != 0) {
        fake_hal_set_reserve_result(HAL_RESERVE_NO_SPACE);
    }
    fixture_library_open(&fixture);
    player_init(&player, &fixture.library);
    BuilderScratch provided = {scratch_read, scratch_write, &scratch, sizeof scratch.bytes};
    if (size > 2 && (data[2] & 1u) != 0) {
        provided = (BuilderScratch){0};
    }
    discovery_screen_init(&screen, (DiscoveryScreenConfig){.library = &fixture.library,
                                                           .player = &player,
                                                           .theme = &theme,
                                                           .motion = &motion,
                                                           .scratch = provided});
    BrowseQuery query = browse_root_query((BrowseKind)(data[0] % ROOT_COUNT));
    switch ((size > 1 ? data[1] : data[0]) % 5) {
    case 0:
        discovery_screen_open(&screen, query, false);
        break;
    case 1:
        discovery_screen_open(&screen, query, true);
        break;
    case 2:
        discovery_screen_options(&screen, query);
        break;
    case 3:
        discovery_screen_info(&screen, data[0] % library_count(&fixture.library, LIBRARY_TRACKS));
        break;
    default:
        discovery_screen_tags(&screen, data[0] % library_count(&fixture.library, LIBRARY_TRACKS));
        break;
    }
    screen.screen.enter(&screen, 0);
    draw_screen(&screen, &theme);
    size_t limit = size < ACTION_LIMIT ? size : ACTION_LIMIT;
    for (size_t index = CONTROL_BYTES; index < limit; index++) {
        apply_action(&screen, &scratch, data[index], (uint32_t)index * 251u);
        if ((data[index] & 7u) == 0) {
            draw_screen(&screen, &theme);
        }
        check_screen(&screen, &fixture);
    }
    return 0;
}
