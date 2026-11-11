#include "gfx/text.h"
#include "ui/label.h"
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "screens/card_format_screen.h"
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
    fake_hal_set_card(
        (HalCardInfo){.present = true, .sectors = 64u * 2 * 1024 * 1024, .generation = 1}, true);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 80});
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

static CardFormatScreen confirmation;

static void open_format(void)
{
    open_settings(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE);
    card_format_screen_init(&confirmation, (CardFormatConfig){.player = &player, .theme = &theme});
}

static ScreenAction press(Key key)
{
    KeyEvent event = {.key = key, .gesture = KEY_GESTURE_PRESS};
    return confirmation.screen.handle_key(&confirmation, event, 1000);
}

static void confirming_twice_with_cancel_selected_does_not_erase(void)
{
    open_format();
    CHECK(strstr(confirmation.message, "Everything") != NULL);
    CHECK(strstr(confirmation.message, "GB") != NULL);
    CHECK(strstr(confirmation.message, "exFAT") != NULL);
    press(KEY_PLAY);
    CHECK(!confirmation.erase);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_PLAY).type);
    CHECK_EQUAL(0, fake_hal_card_formats());
}

static void formatting_requires_the_second_confirmation_and_an_explicit_erase_choice(void)
{
    open_format();
    press(KEY_VOLUME_UP);
    CHECK_EQUAL(0, fake_hal_card_formats());
    press(KEY_PLAY);
    CHECK_EQUAL(0, fake_hal_card_formats());
    press(KEY_VOLUME_UP);
    CHECK(confirmation.erase);
    press(KEY_PLAY);
    CHECK_EQUAL(1, fake_hal_card_formats());
}

static void back_from_either_confirmation_does_not_erase(void)
{
    open_format();
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
    CHECK_EQUAL(0, fake_hal_card_formats());
    open_format();
    press(KEY_PLAY);
    press(KEY_VOLUME_UP);
    CHECK_EQUAL(SCREEN_ACTION_POP, press(KEY_BACK).type);
    CHECK_EQUAL(0, fake_hal_card_formats());
}

static void playback_usb_low_and_unknown_battery_and_a_missing_card_block_erasure(void)
{
    for (int refusal = 0; refusal < 5; refusal++) {
        open_format();
        if (refusal == 0) {
            player.state = PLAYER_PLAYING;
        } else if (refusal == 1) {
            fake_hal_set_usb_session(true);
        } else if (refusal == 2 || refusal == 3) {
            fake_hal_set_battery((HalBattery){.known = refusal == 2, .percent = 30});
        } else {
            fake_hal_set_card((HalCardInfo){0}, true);
        }
        press(KEY_PLAY);
        press(KEY_VOLUME_UP);
        press(KEY_PLAY);
        CHECK_EQUAL(0, fake_hal_card_formats());
    }
}

static void the_final_confirmation_rechecks_card_identity_and_safety(void)
{
    open_format();
    press(KEY_PLAY);
    press(KEY_VOLUME_UP);
    fake_hal_set_card(
        (HalCardInfo){.present = true, .sectors = confirmation.card.sectors, .generation = 2},
        true);
    press(KEY_PLAY);
    CHECK_EQUAL(0, fake_hal_card_formats());
    open_format();
    press(KEY_PLAY);
    press(KEY_VOLUME_UP);
    fake_hal_set_battery((HalBattery){.known = true, .percent = 30});
    press(KEY_PLAY);
    CHECK_EQUAL(0, fake_hal_card_formats());
}

static void charger_power_allows_formatting_and_failures_are_reported(void)
{
    open_format();
    fake_hal_set_battery((HalBattery){.usb_power = true});
    fake_hal_set_card(confirmation.card, false);
    press(KEY_PLAY);
    press(KEY_VOLUME_UP);
    press(KEY_PLAY);
    CHECK_EQUAL(1, fake_hal_card_formats());
    CHECK(strstr(confirmation.message, "format") != NULL);
}

static bool captures(const char *directory)
{
    static Color pixels[480 * 480];
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int orientation = 0; orientation < ORIENTATION_COUNT; orientation++) {
            open_settings((TextSize)size, (Orientation)orientation);
            card_format_screen_init(&confirmation,
                                    (CardFormatConfig){.player = &player, .theme = &theme});
            Rect area = {0, 0, theme.metrics.screen_width, theme.metrics.screen_height};
            Canvas canvas = canvas_make(pixels, area);
            int available = theme_content_area(&theme).height - 2 * theme.metrics.row_height;
            CHECK(label_wrap_count(theme.fonts.small, confirmation.message,
                                   area.width - 2 * theme.metrics.padding) *
                          font_line_height(theme.fonts.small) +
                      theme.metrics.padding <=
                  available);
            for (int phase = 0; phase < 2; phase++) {
                canvas_clear(&canvas, theme.colors.background);
                confirmation.screen.draw(&confirmation, &canvas);
                char path[1024];
                snprintf(path, sizeof path, "%s/format-%d-%d-%d.ppm", directory, size, orientation,
                         phase);
                FILE *file = fopen(path, "wb");
                CHECK(file != NULL);
                if (file == NULL) {
                    return false;
                }
                fprintf(file, "P6\n%d %d\n255\n", area.width, area.height);
                for (int index = 0; index < area.width * area.height; index++) {
                    uint16_t pixel = pixels[index];
                    uint8_t rgb[] = {(uint8_t)((pixel >> 11) * 255 / 31),
                                     (uint8_t)(((pixel >> 5) & 63) * 255 / 63),
                                     (uint8_t)((pixel & 31) * 255 / 31)};
                    fwrite(rgb, 1, sizeof rgb, file);
                }
                fclose(file);
                press(KEY_PLAY);
            }
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    static const TestCase cases[] = {
        TEST_CASE(settings_offers_card_formatting_beside_the_other_card_actions),
        TEST_CASE(confirming_twice_with_cancel_selected_does_not_erase),
        TEST_CASE(formatting_requires_the_second_confirmation_and_an_explicit_erase_choice),
        TEST_CASE(back_from_either_confirmation_does_not_erase),
        TEST_CASE(playback_usb_low_and_unknown_battery_and_a_missing_card_block_erasure),
        TEST_CASE(the_final_confirmation_rechecks_card_identity_and_safety),
        TEST_CASE(charger_power_allows_formatting_and_failures_are_reported),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    if (argc == 3 && strcmp(argv[1], "--captures") == 0) {
        if (!captures(argv[2])) {
            return 1;
        }
    }
    return result;
}
