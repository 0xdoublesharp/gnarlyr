#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "settings_screens.h"

enum {
    PROMPT_MS = 15000
};

static SettingsScreens screens;
static SettingsStore store;
static Player player;
static Toast toast;
static Theme theme;

static Screen *open_chooser(bool prompt)
{
    fake_hal_reset();
    i18n_detach();
    theme = BOARD_THEME;
    store.settings = settings_defaults();
    SettingsContext context = {.store = &store, .player = &player, .toast = &toast};
    settings_screens_init(&screens, context, &theme);
    Screen *screen = settings_screens_usb_modes(&screens, prompt);
    screen->enter(screen->context, 0);
    return screen;
}

static const char *detail(int row)
{
    const ChoiceScreen *choice = &screens.usb.choice;
    return choice->model.detail(choice->model.context, row);
}

static const char *label(int row)
{
    const ChoiceScreen *choice = &screens.usb.choice;
    return choice->model.label(choice->model.context, row);
}

static ScreenAction press(Screen *screen, Key key, uint32_t now_ms)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = KEY_GESTURE_PRESS},
                              now_ms);
}

static void the_prompt_offers_charge_only_file_transfer_and_the_dac(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("USB connected", screen->title) == 0);
    CHECK(strcmp("Charge only", label(0)) == 0);
    CHECK(strcmp("File transfer", label(1)) == 0);
    CHECK(strcmp("USB DAC", label(2)) == 0);
    CHECK_EQUAL(0, screens.usb.choice.list.selected);
}

static void the_prompt_counts_down_on_the_charge_only_row(void)
{
    Screen *screen = open_chooser(true);
    CHECK(strcmp("15 s", detail(0)) == 0);
    CHECK(detail(1) == NULL);
    screen->update(screen->context, 10000);
    CHECK(strcmp("5 s", detail(0)) == 0);
    CHECK(screens.usb.redraw);
    CHECK(!screens.usb.expired);
}

static void the_prompt_lapses_after_fifteen_quiet_seconds(void)
{
    Screen *screen = open_chooser(true);
    screen->update(screen->context, PROMPT_MS - 1);
    CHECK(!screens.usb.expired);
    screen->update(screen->context, PROMPT_MS);
    CHECK(screens.usb.expired);
}

static void a_key_starts_the_countdown_again(void)
{
    Screen *screen = open_chooser(true);
    press(screen, KEY_VOLUME_UP, 10000);
    CHECK(strcmp("15 s", detail(0)) == 0);
    screen->update(screen->context, PROMPT_MS + 9999);
    CHECK(!screens.usb.expired);
    screen->update(screen->context, PROMPT_MS + 10000);
    CHECK(screens.usb.expired);
}

static void choosing_file_transfer_asks_for_a_storage_session(void)
{
    Screen *screen = open_chooser(true);
    press(screen, KEY_VOLUME_UP, 100);
    ScreenAction action = press(screen, KEY_PLAY, 200);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK_EQUAL(1, fake_hal_usb_mode_requests());
    CHECK_EQUAL(HAL_USB_MODE_STORAGE, fake_hal_requested_usb_mode());
}

static void back_leaves_the_prompt_without_a_request(void)
{
    Screen *screen = open_chooser(true);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(screen, KEY_BACK, 100).type);
    CHECK_EQUAL(0, fake_hal_usb_mode_requests());
}

static void usb_mode_opened_by_hand_neither_counts_down_nor_lapses(void)
{
    Screen *screen = open_chooser(false);
    CHECK(strcmp("USB mode", screen->title) == 0);
    CHECK(strcmp("Charge only", label(0)) == 0);
    screen->update(screen->context, 600000);
    CHECK(!screens.usb.expired);
    CHECK(detail(1) == NULL);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_prompt_offers_charge_only_file_transfer_and_the_dac),
        TEST_CASE(the_prompt_counts_down_on_the_charge_only_row),
        TEST_CASE(the_prompt_lapses_after_fifteen_quiet_seconds),
        TEST_CASE(a_key_starts_the_countdown_again),
        TEST_CASE(choosing_file_transfer_asks_for_a_storage_session),
        TEST_CASE(back_leaves_the_prompt_without_a_request),
        TEST_CASE(usb_mode_opened_by_hand_neither_counts_down_nor_lapses),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
