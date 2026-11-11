#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "screens/cassette_flip_screen.h"
#include "text_spy.h"

static FixtureLibrary fixture;
static Player player;
static MixtapeUi mixtapes;
static CassetteFlipScreen flip;
static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];

static void every_flip_prompt_fits_both_orientations_and_all_text_sizes(void)
{
    fake_hal_reset();
    player_init(&player, fixture_library_open(&fixture));
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < 3; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            for (int side = 0; side < 2; side++) {
                player.cassette =
                    (CassetteState){.active = true, .flip = true, .side = (uint8_t)side};
                Screen *screen = cassette_flip_screen_init(&flip, &player, &theme, &mixtapes);
                screen->enter(screen->context, 0);
                Canvas canvas = canvas_make(
                    pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
                text_spy_reset();
                screen->draw(screen->context, &canvas);
                CHECK(text_spy_count() > 2);
                for (int index = 0; index < text_spy_count(); index++) {
                    CHECK(strstr(text_spy_entry(index)->shown, "…") == NULL);
                }
            }
        }
    }
}

static void every_length_choice_shows_its_complete_budget(void)
{
    fake_hal_reset();
    player_init(&player, fixture_library_open(&fixture));
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < 3; size++) {
            Theme theme = BOARD_THEME;
            default_theme_set_text_size(&theme, (TextSize)size);
            default_theme_set_orientation(&theme, (Orientation)orientation);
            Screen *screen = mixtape_ui_api()->init(
                &mixtapes, (MixtapeUiConfig){.player = &player, .theme = &theme});
            screen->enter(screen->context, 0);
            list_view_select(&mixtapes.list, 2, 0);
            screen->handle_key(screen->context,
                               (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS}, 1);
            Canvas canvas = canvas_make(
                pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
            text_spy_reset();
            screen->draw(screen->context, &canvas);
            CHECK(text_spy_count() >= 5);
            for (int index = 0; index < text_spy_count(); index++) {
                CHECK(strstr(text_spy_entry(index)->shown, "…") == NULL);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_flip_prompt_fits_both_orientations_and_all_text_sizes),
        TEST_CASE(every_length_choice_shows_its_complete_budget),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
