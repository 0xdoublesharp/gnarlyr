#include <stddef.h>

#include "app_screens.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "screen_modules.h"

static AppScreens screens;
static LibraryStore store;
static Player player;
static CoverCache covers;
static FavoritesStore favorites;
static Toast toast;
static SettingsStore settings;
static PowerPolicy power;
static BluetoothLink bluetooth;
static AppOptions options;
static Theme styled;
static bool rescan_requested;
static bool language_requested;

static void reset(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_reset();
    fake_hal_add_file(LIBRARY_STORE_PATH, bytes, size);
    library_store_open(&store);
    styled = BOARD_THEME;
    settings.settings = settings_defaults();
    screens = (AppScreens){.context = {.options = &options,
                                       .theme = &BOARD_THEME,
                                       .styled_theme = &styled,
                                       .compact_theme = &BOARD_THEME,
                                       .store = &store,
                                       .player = &player,
                                       .settings = &settings,
                                       .favorites = &favorites,
                                       .power = &power,
                                       .toast = &toast,
                                       .covers = &covers,
                                       .rescan_requested = &rescan_requested,
                                       .language_requested = &language_requested,
                                       .bluetooth = &bluetooth}};
}

static void screens_outside_every_module_belong_to_none(void)
{
    reset();
    Screen elsewhere = {0};
    CHECK_EQUAL(SCREEN_MODULE_NONE, screen_modules_of(&screens, &screens.home.screen));
    CHECK_EQUAL(SCREEN_MODULE_NONE, screen_modules_of(&screens, &screens.now_playing.screen));
    CHECK_EQUAL(SCREEN_MODULE_NONE, screen_modules_of(&screens, &screens.no_library.screen));
    CHECK_EQUAL(SCREEN_MODULE_NONE, screen_modules_of(&screens, &elsewhere));
}

static void a_screen_belongs_to_the_module_whose_memory_holds_it(void)
{
    reset();
    const PlayingModuleScreens *views = &screens.modules.playing;
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &views->cover_flow.screen));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &views->up_next.screen));
}

static void screens_a_module_builds_for_itself_belong_to_it_too(void)
{
    reset();
    const SettingsModuleScreens *own = &screens.modules.settings;
    const Screen *inside[] = {&own->settings.menu.screen, &own->settings.choice.screen,
                              &own->settings.equalizer.screen, &own->bluetooth.screen};
    for (size_t index = 0; index < sizeof inside / sizeof inside[0]; index++) {
        CHECK_EQUAL(SCREEN_MODULE_SETTINGS, screen_modules_of(&screens, inside[index]));
    }
}

static void every_module_has_its_own_memory(void)
{
    reset();
    for (int module = SCREEN_MODULE_PLAYING; module <= SCREEN_MODULE_SETTINGS; module++) {
        int held = 0;
        for (size_t at = 0; at < sizeof screens; at += sizeof(void *)) {
            const Screen *screen = (const Screen *)((const char *)&screens + at);
            held += screen_modules_of(&screens, screen) == (ScreenModule)module ? 1 : 0;
        }
        CHECK(held > 0);
    }
}

static void each_module_builds_its_own_screens(void)
{
    reset();
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_SETTINGS) != NULL);
    CHECK(screens.modules.settings.settings.menu.screen.draw != NULL);
    CHECK(screens.modules.settings.bluetooth.screen.draw != NULL);
    CHECK(screens.modules.views.diagnostics.screen.draw == NULL);
    CHECK_EQUAL(SCREEN_MODULE_VIEWS,
                screen_modules_of(&screens, &screens.modules.views.diagnostics.screen));
    CHECK(screens.modules.playing.cover_flow.screen.draw == NULL);
    CHECK_EQUAL(SCREEN_MODULE_SETTINGS, (int)fake_hal_loaded_module());
}

static void the_first_and_the_last_byte_of_a_modules_memory_are_inside_it(void)
{
    reset();
    const char *first = (const char *)&screens.modules.playing;
    const char *last = first + sizeof screens.modules.playing - 1;
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, (const Screen *)first));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, (const Screen *)last));
    CHECK(screen_modules_of(&screens, (const Screen *)(first - 1)) != SCREEN_MODULE_PLAYING);
    CHECK(screen_modules_of(&screens, (const Screen *)(last + 1)) != SCREEN_MODULE_PLAYING);
}

static void using_a_module_loads_it_and_builds_its_screens(void)
{
    reset();
    CHECK(screens.modules.playing.cover_flow.screen.draw == NULL);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_PLAYING) != NULL);
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK(screens.modules.playing.cover_flow.screen.draw != NULL);
    CHECK(screens.modules.playing.up_next.screen.draw != NULL);
    CHECK(screens.modules.views.diagnostics.screen.draw == NULL);
}

static void a_module_builds_its_screens_once_until_the_screens_are_built_again(void)
{
    reset();
    screen_modules_use(&screens, SCREEN_MODULE_PLAYING);
    screens.modules.playing.cover_flow.target = 3;
    screen_modules_use(&screens, SCREEN_MODULE_PLAYING);
    CHECK_EQUAL(3, screens.modules.playing.cover_flow.target);
    screens.bound_modules = 0;
    screen_modules_use(&screens, SCREEN_MODULE_PLAYING);
    CHECK_EQUAL(0, screens.modules.playing.cover_flow.target);
    CHECK_EQUAL(1, fake_hal_module_loads());
}

static void a_module_that_cannot_load_is_not_used_and_builds_nothing(void)
{
    reset();
    fake_hal_break_module(SCREEN_MODULE_PLAYING, true);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_PLAYING) == NULL);
    CHECK(screens.modules.playing.cover_flow.screen.draw == NULL);
    CHECK_EQUAL(0, (int)screens.bound_modules);
    fake_hal_break_module(SCREEN_MODULE_PLAYING, false);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_PLAYING) != NULL);
}

static void the_eleventh_module_loads_independently_of_views(void)
{
    reset();
    CHECK_EQUAL(11, (int)SCREEN_MODULE_PLAYING);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_PLAYING) != NULL);
    CHECK_EQUAL(11, (int)fake_hal_loaded_module());
    CHECK(screens.modules.views.diagnostics.screen.draw == NULL);
    CHECK(screens.modules.playing.cover_flow.screen.draw != NULL);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_VIEWS) != NULL);
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, (int)fake_hal_loaded_module());
    CHECK(screens.modules.views.diagnostics.screen.draw != NULL);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(screens_outside_every_module_belong_to_none),
        TEST_CASE(a_screen_belongs_to_the_module_whose_memory_holds_it),
        TEST_CASE(screens_a_module_builds_for_itself_belong_to_it_too),
        TEST_CASE(every_module_has_its_own_memory),
        TEST_CASE(each_module_builds_its_own_screens),
        TEST_CASE(the_first_and_the_last_byte_of_a_modules_memory_are_inside_it),
        TEST_CASE(using_a_module_loads_it_and_builds_its_screens),
        TEST_CASE(a_module_builds_its_screens_once_until_the_screens_are_built_again),
        TEST_CASE(a_module_that_cannot_load_is_not_used_and_builds_nothing),
        TEST_CASE(the_eleventh_module_loads_independently_of_views),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
