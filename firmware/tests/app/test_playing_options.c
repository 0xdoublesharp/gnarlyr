#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "playing_options.h"

static FixtureLibrary fixture;
static Player player;
static SettingsStore store;
static Toast toast;
static PlayingOptions options;
static Screen lyrics;
static Theme theme;

static void start(int saved_speed)
{
    fake_hal_reset();
    theme = BOARD_THEME;
    player_init(&player, fixture_library_open(&fixture));
    settings_store_load(&store);
    store.settings.playback_speed = (uint8_t)saved_speed;
    playing_options_init(&options,
                         (PlayingOptionsConfig){&player, &store, &toast, &lyrics, &theme});
}

static const char *speed_row_value(void)
{
    const MenuItem *row = &options.items[PLAYING_OPTIONS_SPEED_ITEM];
    return row->value(row->context);
}

static void the_speed_row_shows_the_saved_speed(void)
{
    start(SETTINGS_SPEED_NORMAL);
    CHECK(strcmp("Normal (1\xC3\x97)", speed_row_value()) == 0);
    start(SETTINGS_SPEED_COUNT - 1);
    CHECK(strcmp("3\xC3\x97", speed_row_value()) == 0);
}

static void choosing_a_speed_applies_saves_and_shows_it(void)
{
    start(SETTINGS_SPEED_NORMAL);
    options.speed.config.choose(options.speed.config.context, 7);
    CHECK_EQUAL(250, fake_hal_audio_speed());
    CHECK_EQUAL(250, player_status(&player).speed);
    CHECK(strcmp("2.5\xC3\x97", speed_row_value()) == 0);
    SettingsStore saved;
    settings_store_load(&saved);
    CHECK_EQUAL(7, saved.settings.playback_speed);
}

static void the_lyrics_row_opens_lyrics(void)
{
    start(SETTINGS_SPEED_NORMAL);
    CHECK(options.items[PLAYING_OPTIONS_LYRICS_ITEM].context == &lyrics);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_speed_row_shows_the_saved_speed),
        TEST_CASE(choosing_a_speed_applies_saves_and_shows_it),
        TEST_CASE(the_lyrics_row_opens_lyrics),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
