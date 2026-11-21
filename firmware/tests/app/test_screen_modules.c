#include <stddef.h>

#include "app_screens.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "language.h"
#include "modules/modules.h"
#include "screen_modules.h"
#include "screens/cassette_now_playing.h"
#include "screens/meter_now_playing.h"
#include "test_pack.h"

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
    CHECK_EQUAL(SCREEN_MODULE_NONE, screen_modules_of(&screens, &screens.no_library.screen));
    CHECK_EQUAL(SCREEN_MODULE_NONE, screen_modules_of(&screens, &elsewhere));
}

static void a_screen_belongs_to_the_module_whose_memory_holds_it(void)
{
    reset();
    const PlayingModuleScreens *views = &screens.modules.playing;
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &views->cover_flow.screen));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &views->up_next.screen));
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &screens.now_playing.screen));
}

static void screens_a_module_builds_for_itself_belong_to_it_too(void)
{
    reset();
    const SettingsModuleScreens *own = &screens.modules.settings;
    const Screen *inside[] = {&own->settings.menu.screen, &own->settings.choice.screen,
                              &own->settings.clock.screen, &own->bluetooth.screen};
    for (size_t index = 0; index < sizeof inside / sizeof inside[0]; index++) {
        CHECK_EQUAL(SCREEN_MODULE_SETTINGS, screen_modules_of(&screens, inside[index]));
    }
}

static void equalizer_screens_belong_to_their_dedicated_module(void)
{
    reset();
    const EqualizerUi *ui = &screens.modules.equalizer;
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, screen_modules_of(&screens, &ui->browser.choice.screen));
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, screen_modules_of(&screens, &ui->graphic.screen));
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, screen_modules_of(&screens, &ui->parametric.screen));
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_EQUALIZER) != NULL);
    CHECK(screens.modules.equalizer.parametric.screen.draw != NULL);
    CHECK(screens.modules.playing.cover_flow.screen.draw == NULL);
    CHECK_EQUAL(SCREEN_MODULE_EQUALIZER, (int)fake_hal_loaded_module());
}

static void every_module_has_its_own_memory(void)
{
    reset();
    static const ScreenModule MODULES[] = {SCREEN_MODULE_VIEWS,   SCREEN_MODULE_SETTINGS,
                                           SCREEN_MODULE_LIBRARY, SCREEN_MODULE_LISTENING,
                                           SCREEN_MODULE_PLAYING, SCREEN_MODULE_EQUALIZER};
    for (size_t index = 0; index < sizeof MODULES / sizeof MODULES[0]; index++) {
        ScreenModule module = MODULES[index];
        int held = 0;
        for (size_t at = 0; at < sizeof screens; at += sizeof(void *)) {
            const Screen *screen = (const Screen *)((const char *)&screens + at);
            held += screen_modules_of(&screens, screen) == module ? 1 : 0;
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

static void a_module_builds_its_screens_again_when_its_text_comes_from_another_pack(void)
{
    static uint8_t english[TEST_PACK_CAPACITY];
    static uint8_t upper[TEST_PACK_CAPACITY];
    static const char path[] = "/" FIRMWARE_DATA_DIRECTORY "/lang/up.pack";
    reset();
    i18n_detach();
    uint32_t english_size = test_pack_build(english, (TestPackLanguage){"en", "English", NULL});
    uint32_t upper_size =
        test_pack_build(upper, (TestPackLanguage){"up", "UPPER", test_pack_upper_case});
    fake_hal_add_language_pack(0, english, english_size);
    fake_hal_add_language_file("up.pack", upper, upper_size);
    CHECK(language_load("up"));
    screen_modules_use(&screens, SCREEN_MODULE_PLAYING);
    screens.modules.playing.cover_flow.target = 3;
    screen_modules_use(&screens, SCREEN_MODULE_VIEWS);
    screen_modules_use(&screens, SCREEN_MODULE_PLAYING);
    CHECK_EQUAL(3, screens.modules.playing.cover_flow.target);
    fake_hal_fail_file_opens(path);
    screen_modules_use(&screens, SCREEN_MODULE_VIEWS);
    screens.modules.playing.cover_flow.target = 3;
    screen_modules_use(&screens, SCREEN_MODULE_PLAYING);
    CHECK_EQUAL(0, screens.modules.playing.cover_flow.target);
    CHECK(language_is("up"));
    fake_hal_fail_file_opens(NULL);
    i18n_detach();
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

static const PlayingModuleEntry *attach_now_playing(NowPlayingStyle style)
{
    settings_set_now_playing_style(&settings.settings, style);
    const PlayingModuleEntry *entry = (const PlayingModuleEntry *)screen_modules_use(
        &screens, screen_modules_of(&screens, &screens.now_playing.screen));
    if (entry != NULL) {
        entry->now_playing_attach(&screens.now_playing);
    }
    return entry;
}

static void cassette_now_playing_requires_playing_and_reattaches_after_other_modules(void)
{
    reset();
    screens.now_playing.config.settings = &settings.settings;
    CHECK(attach_now_playing(NOW_PLAYING_CASSETTE) != NULL);
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK(screens.now_playing.config.cassette_draw == cassette_now_playing_draw);
    CHECK(screens.now_playing.config.cassette_animate == cassette_now_playing_animate);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_SETTINGS) != NULL);
    CHECK(attach_now_playing(NOW_PLAYING_CASSETTE) != NULL);
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK(attach_now_playing(NOW_PLAYING_COVER) != NULL);
    CHECK(screens.now_playing.config.cassette_draw == NULL);
    CHECK(screens.now_playing.config.cassette_animate == NULL);
}

static void meter_styles_draw_now_playing_and_lists_from_the_stage_module(void)
{
    reset();
    screens.now_playing.config.settings = &settings.settings;
    const PlayingModuleScreens *views = &screens.modules.playing;
    const Screen *list = &screens.browse.screens[0].screen;
    for (int style = NOW_PLAYING_COVER; style <= NOW_PLAYING_SPECTRUM; style++) {
        settings_set_now_playing_style(&settings.settings, (NowPlayingStyle)style);
        ScreenModule expected =
            style >= NOW_PLAYING_VU ? SCREEN_MODULE_STAGE : SCREEN_MODULE_PLAYING;
        CHECK_EQUAL(expected, screen_modules_of(&screens, &screens.now_playing.screen));
        CHECK_EQUAL(expected, screen_modules_of(&screens, list));
        CHECK_EQUAL(expected, screen_modules_playing(&screens));
        CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &views->cover_flow.screen));
        CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &views->up_next.screen));
        CHECK_EQUAL(SCREEN_MODULE_PLAYING,
                    screen_modules_of(&screens, &views->artist_albums.screen));
    }
}

static void each_playing_module_attaches_its_own_now_playing_hooks(void)
{
    reset();
    screens.now_playing.config.settings = &settings.settings;
    CHECK(attach_now_playing(NOW_PLAYING_VU) == &STAGE_MODULE_ENTRY);
    CHECK_EQUAL(SCREEN_MODULE_STAGE, (int)fake_hal_loaded_module());
    CHECK(screens.now_playing.config.cassette_draw == meter_now_playing_draw);
    CHECK(screens.now_playing.config.cassette_animate == meter_now_playing_animate);
    CHECK(screens.now_playing.screen.draw != NULL);
    CHECK(attach_now_playing(NOW_PLAYING_CASSETTE) == &PLAYING_MODULE_ENTRY);
    CHECK(screens.now_playing.config.cassette_draw == cassette_now_playing_draw);
    CHECK(attach_now_playing(NOW_PLAYING_SPECTRUM) == &STAGE_MODULE_ENTRY);
    CHECK(screens.now_playing.config.cassette_draw == meter_now_playing_draw);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_PLAYING) != NULL);
    CHECK(screens.modules.playing.cover_flow.screen.draw != NULL);
    screens.modules.playing.cover_flow.target = 3;
    CHECK(attach_now_playing(NOW_PLAYING_VU) == &STAGE_MODULE_ENTRY);
    CHECK(screen_modules_use(&screens, SCREEN_MODULE_PLAYING) != NULL);
    CHECK_EQUAL(3, screens.modules.playing.cover_flow.target);
    CHECK_EQUAL(6, fake_hal_module_loads());
}

static void the_resident_fallback_drops_every_style_hook(void)
{
    reset();
    screens.now_playing.config.settings = &settings.settings;
    for (int style = NOW_PLAYING_CASSETTE; style <= NOW_PLAYING_SPECTRUM; style++) {
        CHECK(attach_now_playing((NowPlayingStyle)style) != NULL);
        CHECK(screens.now_playing.config.cassette_animate != NULL);
        now_playing_fallback_attach(&screens.now_playing);
        CHECK(screens.now_playing.config.cassette_draw == NULL);
        CHECK(screens.now_playing.config.cassette_animate == NULL);
    }
}

static void a_damaged_stage_module_hands_meter_styles_to_playing_and_is_not_retried(void)
{
    reset();
    screens.now_playing.config.settings = &settings.settings;
    fake_hal_break_module(SCREEN_MODULE_STAGE, true);
    CHECK(attach_now_playing(NOW_PLAYING_VU) == &PLAYING_MODULE_ENTRY);
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, (int)fake_hal_loaded_module());
    CHECK(screens.now_playing.config.cassette_draw == NULL);
    CHECK_EQUAL(SCREEN_MODULE_PLAYING, screen_modules_of(&screens, &screens.now_playing.screen));
    int refusals = fake_hal_module_refusals();
    for (int draw = 0; draw < 10; draw++) {
        CHECK(screen_modules_use(&screens, screen_modules_playing(&screens)) != NULL);
    }
    CHECK_EQUAL(1, refusals);
    CHECK_EQUAL(refusals, fake_hal_module_refusals());
    CHECK_EQUAL(1, fake_hal_module_loads());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(meter_styles_draw_now_playing_and_lists_from_the_stage_module),
        TEST_CASE(each_playing_module_attaches_its_own_now_playing_hooks),
        TEST_CASE(a_damaged_stage_module_hands_meter_styles_to_playing_and_is_not_retried),
        TEST_CASE(the_resident_fallback_drops_every_style_hook),
        TEST_CASE(cassette_now_playing_requires_playing_and_reattaches_after_other_modules),
        TEST_CASE(screens_outside_every_module_belong_to_none),
        TEST_CASE(a_screen_belongs_to_the_module_whose_memory_holds_it),
        TEST_CASE(screens_a_module_builds_for_itself_belong_to_it_too),
        TEST_CASE(every_module_has_its_own_memory),
        TEST_CASE(equalizer_screens_belong_to_their_dedicated_module),
        TEST_CASE(each_module_builds_its_own_screens),
        TEST_CASE(the_first_and_the_last_byte_of_a_modules_memory_are_inside_it),
        TEST_CASE(using_a_module_loads_it_and_builds_its_screens),
        TEST_CASE(a_module_builds_its_screens_once_until_the_screens_are_built_again),
        TEST_CASE(a_module_builds_its_screens_again_when_its_text_comes_from_another_pack),
        TEST_CASE(a_module_that_cannot_load_is_not_used_and_builds_nothing),
        TEST_CASE(the_eleventh_module_loads_independently_of_views),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
