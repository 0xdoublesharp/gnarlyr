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

static void reset(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_reset();
    fake_hal_add_file(LIBRARY_STORE_PATH, bytes, size);
    library_store_open(&store);
    screens = (AppScreens){.context = {.theme = &BOARD_THEME,
                                       .store = &store,
                                       .player = &player,
                                       .covers = &covers,
                                       .favorites = &favorites,
                                       .toast = &toast}};
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
    const ViewsModuleScreens *views = &screens.modules.views;
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, screen_modules_of(&screens, &views->cover_flow.screen));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, screen_modules_of(&screens, &views->up_next.screen));
}

static void the_first_and_the_last_byte_of_a_modules_memory_are_inside_it(void)
{
    reset();
    const char *first = (const char *)&screens.modules.views;
    const char *last = first + sizeof screens.modules.views - 1;
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, screen_modules_of(&screens, (const Screen *)first));
    CHECK_EQUAL(SCREEN_MODULE_VIEWS, screen_modules_of(&screens, (const Screen *)last));
    CHECK(screen_modules_of(&screens, (const Screen *)(first - 1)) != SCREEN_MODULE_VIEWS);
    CHECK(screen_modules_of(&screens, (const Screen *)(last + 1)) != SCREEN_MODULE_VIEWS);
}

static void using_a_module_loads_it_and_builds_its_screens(void)
{
    reset();
    CHECK(screens.modules.views.cover_flow.screen.draw == NULL);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_VIEWS) != NULL);
    CHECK_EQUAL(1, fake_hal_module_loads());
    CHECK(screens.modules.views.cover_flow.screen.draw != NULL);
    CHECK(screens.modules.views.up_next.screen.draw != NULL);
}

static void a_module_builds_its_screens_once_until_the_screens_are_built_again(void)
{
    reset();
    screen_modules_use(&screens, SCREEN_MODULE_VIEWS);
    screens.modules.views.cover_flow.target = 3;
    screen_modules_use(&screens, SCREEN_MODULE_VIEWS);
    CHECK_EQUAL(3, screens.modules.views.cover_flow.target);
    screens.bound_modules = 0;
    screen_modules_use(&screens, SCREEN_MODULE_VIEWS);
    CHECK_EQUAL(0, screens.modules.views.cover_flow.target);
    CHECK_EQUAL(1, fake_hal_module_loads());
}

static void a_module_that_cannot_load_is_not_used_and_builds_nothing(void)
{
    reset();
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_VIEWS) == NULL);
    CHECK(screens.modules.views.cover_flow.screen.draw == NULL);
    CHECK_EQUAL(0, (int)screens.bound_modules);
    fake_hal_break_module(SCREEN_MODULE_VIEWS, false);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_VIEWS) != NULL);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(screens_outside_every_module_belong_to_none),
        TEST_CASE(a_screen_belongs_to_the_module_whose_memory_holds_it),
        TEST_CASE(the_first_and_the_last_byte_of_a_modules_memory_are_inside_it),
        TEST_CASE(using_a_module_loads_it_and_builds_its_screens),
        TEST_CASE(a_module_builds_its_screens_once_until_the_screens_are_built_again),
        TEST_CASE(a_module_that_cannot_load_is_not_used_and_builds_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
