#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "settings_screens.h"

static SettingsScreens screens;
static SettingsStore store;
static Player player;
static Library library;
static PowerPolicy power;
static Toast toast;
static BluetoothLink link;
static BluetoothScreen bluetooth;
static Theme theme;

static void open_settings(TextSize size, Orientation orientation)
{
    fake_hal_reset();
    i18n_detach();
    theme = BOARD_THEME;
    default_theme_set_text_size(&theme, size);
    default_theme_set_orientation(&theme, orientation);
    store.settings = settings_defaults();
    player_init(&player, &library);
    bluetooth_screen_init(&bluetooth, &link, &theme);
    SettingsContext context = {.store = &store,
                               .player = &player,
                               .power = &power,
                               .toast = &toast,
                               .bluetooth = &bluetooth,
                               .theme = &theme};
    Screen *screen = settings_screens_init(&screens, context, &theme);
    screens.items[SETTINGS_ABOUT_ITEM].label = i18n_text(STR_ABOUT);
    screen->enter(screen->context, 0);
}

static const MenuItem *item_named(const char *name)
{
    for (int index = 0; index < screens.menu.config.item_count; index++) {
        const MenuItem *item = &screens.menu.config.items[index];
        if (item->label != NULL && strcmp(item->label, name) == 0) {
            return item;
        }
    }
    return NULL;
}

static void settings_offers_card_formatting_beside_the_other_card_actions(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            open_settings((TextSize)size, (Orientation)orientation);
            const MenuItem *format = item_named("Format card");
            CHECK(format != NULL);
            if (format != NULL) {
                CHECK(format->opens);
                CHECK(format->select != NULL);
            }
            CHECK(item_named("Rescan library") != NULL);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(settings_offers_card_formatting_beside_the_other_card_actions),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
