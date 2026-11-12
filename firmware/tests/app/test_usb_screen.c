#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screen_modules.h"
#include "screen_snapshot.h"
#include "screens/charge_screen.h"
#include "screens/usb_dac_menu.h"
#include "screens/usb_scene.h"
#include "text_spy.h"
#include "ui/label.h"
#include "ui/wire_motion.h"
#ifdef ECHO_TEST_GLYPHS
#include "sim_glyphs.h"
#endif

enum {
    THEMES = APPEARANCE_CLASSIC + 1,
    GIB_SECTORS = 2097152,
    DAC_RATE = 44100
};

typedef struct Variant {
    AppUsbStatus status;
    UsbScene scene;
    const char *name;
} Variant;

static Color pixels[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];
static Color before[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];
static UsbDacMenu menu;
static int registered_draws;

static bool saw(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->shown, text) == 0) {
            return true;
        }
    }
    return false;
}

static bool saw_start(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        const TextSpyEntry *entry = text_spy_entry(index);
        if (strcmp(entry->rest, text) == 0 && strncmp(entry->shown, text, 1) == 0) {
            return true;
        }
    }
    return false;
}

static Theme theme_for(int appearance, int orientation, int size)
{
    Theme theme = BOARD_THEME;
    default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    return theme;
}

static Canvas canvas_for(const Theme *theme)
{
    return canvas_make(pixels,
                       (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
}

static UsbScene storage_scene(uint8_t state)
{
    return (UsbScene){.state = state,
                      .high_speed = true,
                      .written_kib = 12700,
                      .drives = {{4 * GIB_SECTORS, USB_SCENE_DRIVE_MOUNTED},
                                 {60 * GIB_SECTORS, USB_SCENE_DRIVE_MOUNTED}}};
}

static UsbScene dac_scene(uint8_t state)
{
    UsbScene scene = {.state = state};
    if (state != USB_SCENE_DAC_CHOOSE) {
        scene.dac = (UsbSceneDac){.headphones = "Studio",
                                  .rate = DAC_RATE,
                                  .bits = 24,
                                  .volume = 35,
                                  .host_volume = -12 * 256,
                                  .host_volume_known = true,
                                  .jack = HAL_AUDIO_JACK_SINGLE_ENDED,
                                  .buffer_ms = 20,
                                  .drift_ppm = 12,
                                  .seconds = 754,
                                  .level = {200, 150}};
    }
    return scene;
}

static AppUsbStatus storage_status(StringId detail)
{
    return (AppUsbStatus){.view = APP_USB_STORAGE,
                          .information = i18n_text(STR_POWER_INTERNAL_CARD),
                          .detail = i18n_text(detail)};
}

static int variants(Variant *list)
{
    static const struct {
        uint8_t state;
        StringId detail;
        const char *name;
    } STORAGE[] = {
        {USB_SCENE_CONNECTED, STR_USB_EJECT_DRIVES, "storage-connected"},
        {USB_SCENE_READING, STR_USB_EJECT_DRIVES, "storage-reading"},
        {USB_SCENE_WRITING, STR_USB_EJECT_DRIVES, "storage-copying"},
        {USB_SCENE_WAITING, STR_USB_KEEP_HOLDING, "storage-waiting"},
        {USB_SCENE_LEAVING, STR_USB_LEAVING_HINT, "storage-disconnecting"},
        {USB_SCENE_UNPLUGGED, STR_USB_UNPLUGGED_HINT, "storage-unplugged"},
        {USB_SCENE_STOPPED, STR_USB_STOPPED_HINT, "storage-stopped"},
    };
    int count = 0;
    list[count++] = (Variant){
        .status = {.view = APP_USB_CONNECTING, .detail = i18n_text(STR_PLUG_COMPUTER_LINE)},
        .scene = {.state = USB_SCENE_STARTING},
        .name = "connecting"};
    for (size_t index = 0; index < sizeof STORAGE / sizeof STORAGE[0]; index++) {
        list[count++] = (Variant){storage_status(STORAGE[index].detail),
                                  storage_scene(STORAGE[index].state), STORAGE[index].name};
    }
    list[count] = list[1];
    list[count].scene.drives[1].state = USB_SCENE_DRIVE_EJECTED;
    list[count++].name = "storage-card-ejected";
    static const struct {
        uint8_t state;
        const char *name;
    } DAC[] = {{USB_SCENE_DAC_CHOOSE, "dac-choose"},
               {USB_SCENE_DAC_READY, "dac-idle"},
               {USB_SCENE_DAC_PLAYING, "dac-playing"},
               {USB_SCENE_DAC_FAILED, "dac-failed"}};
    for (size_t index = 0; index < sizeof DAC / sizeof DAC[0]; index++) {
        list[count++] = (Variant){{.view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT)},
                                  dac_scene(DAC[index].state),
                                  DAC[index].name};
    }
    list[count] = list[count - 2];
    list[count].status.detail = i18n_text(STR_QUICK_SETTINGS_HINT);
    list[count].scene.menu = &menu.view;
    list[count++].name = "dac-menu";
    return count;
}

static void rect_fits(Rect area, const Theme *theme)
{
    CHECK(area.width >= 0);
    CHECK(area.height >= 0);
    CHECK(area.x >= 0);
    CHECK(area.y >= 0);
    CHECK(area.x + area.width <= theme->metrics.screen_width);
    CHECK(area.y + area.height <= theme->metrics.screen_height);
}

static bool apart(Rect first, Rect second)
{
    return rect_is_empty(rect_intersection(first, second));
}

static void layout_holds_together(const Theme *theme, AppUsbStatus status)
{
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    rect_fits(layout.header, theme);
    rect_fits(layout.art, theme);
    rect_fits(layout.figure, theme);
    CHECK(rect_is_empty(rect_intersection(layout.figure, layout.state)));
    rect_fits(layout.meter, theme);
    rect_fits(layout.state, theme);
    rect_fits(layout.lines, theme);
    rect_fits(layout.detail, theme);
    CHECK(layout.art.width >= WIRE_MOTION_MIN_SIDE);
    CHECK(apart(layout.art, layout.state));
    CHECK(apart(layout.meter, layout.state));
    CHECK(apart(layout.art, layout.lines));
    CHECK(apart(layout.state, layout.detail));
    CHECK(apart(layout.lines, layout.detail));
    CHECK(layout.header.y + layout.header.height <= layout.art.y);
    CHECK(layout.lines.height >= font_line_height(theme->fonts.small));
}

static void check_texts(const Variant *variant)
{
    CHECK(saw(charge_screen_usb_title(variant->status.view)));
    CHECK(saw_start(variant->status.detail));
    if (variant->status.view == APP_USB_STORAGE && variant->scene.state <= USB_SCENE_LEAVING) {
        CHECK(saw(i18n_text(STR_POWER_INTERNAL)));
    }
    if (variant->scene.state == USB_SCENE_DAC_PLAYING && variant->scene.menu == NULL) {
        CHECK(saw(i18n_text(STR_PLAYING)));
        CHECK(saw(i18n_text(STR_VOLUME)));
        CHECK(saw("35"));
    }
}

static void every_usb_state_fits_every_theme_size_and_orientation(void)
{
#ifdef ECHO_TEST_GLYPHS
    sim_glyphs_attach();
#endif
    Variant list[16];
    int count = variants(list);
    int shot = 0;
    for (int appearance = 0; appearance < THEMES; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(appearance, orientation, size);
                Canvas canvas = canvas_for(&theme);
                usb_dac_menu_open(
                    &menu,
                    (UsbDacMenuConfig){&theme, (AppDacSettings){35, 0, 0, HEADPHONE_PROFILE_NONE},
                                       NULL},
                    0);
                for (int index = 0; index < count; index++) {
                    AppUsbStatus status = list[index].status;
                    status.scene = &list[index].scene;
                    layout_holds_together(&theme, status);
                    text_spy_reset();
                    usb_screen_draw(
                        &canvas, &theme,
                        (HalBattery){
                            .known = true, .percent = 64, .usb_power = true, .charging = true},
                        status);
                    check_texts(&list[index]);
                    screen_snapshot_write(&canvas, list[index].name, shot + index);
                }
                shot += 100;
            }
        }
    }
}

static void registered_usb_draw(Canvas *canvas, const Theme *theme, HalBattery battery,
                                AppUsbStatus status)
{
    registered_draws++;
    usb_screen_draw(canvas, theme, battery, status);
}

static void connecting_uses_the_overlay_and_a_cleared_renderer_the_resident_fallback(void)
{
    fake_hal_reset();
    app_start();
    registered_draws = 0;
    app_set_usb_renderer(registered_usb_draw);
    AppUsbStatus status = {.view = APP_USB_CONNECTING, .detail = i18n_text(STR_PLUG_COMPUTER_LINE)};
    int loads = fake_hal_module_loads();
    app_show_usb_status(status);
    CHECK_EQUAL(1, registered_draws > 0);
    int called = registered_draws;
    app_set_usb_renderer(NULL);
    fake_hal_break_module(SCREEN_MODULE_SETTINGS, true);
    app_show_usb_status(status);
    CHECK_EQUAL(called, registered_draws);
    CHECK_EQUAL(loads, fake_hal_module_loads());
    fake_hal_break_module(SCREEN_MODULE_SETTINGS, false);
    app_stop();
}

static void partial_redraws_are_split_into_bounded_horizontal_bands(void)
{
    fake_hal_reset();
    app_start();
    registered_draws = 0;
    app_set_usb_renderer(registered_usb_draw);
    AppUsbStatus status = {.view = APP_USB_CONNECTING, .detail = i18n_text(STR_PLUG_COMPUTER)};
    usb_screen_show_in_bands(status, (Rect){0, 0, 20, 0});
    CHECK_EQUAL(0, registered_draws);
    usb_screen_show_in_bands(status, (Rect){0, 0, 20, 1});
    CHECK_EQUAL(1, registered_draws);
    usb_screen_show_in_bands(status, (Rect){0, 0, 20, 100});
    CHECK(registered_draws > 2);
    app_set_usb_renderer(NULL);
    app_stop();
}

static Theme medium(int orientation)
{
    return theme_for(APPEARANCE_NEON_NOIR, orientation, TEXT_SIZE_MEDIUM);
}

static bool changes_stay_inside(const Theme *theme, Rect area)
{
    for (int y = 0; y < theme->metrics.screen_height; y++) {
        for (int x = 0; x < theme->metrics.screen_width; x++) {
            int index = y * theme->metrics.screen_width + x;
            if (pixels[index] != before[index] &&
                rect_is_empty(rect_intersection(area, (Rect){x, y, 1, 1}))) {
                return false;
            }
        }
    }
    return true;
}

static bool changed(const Theme *theme)
{
    int count = theme->metrics.screen_width * theme->metrics.screen_height;
    return memcmp(pixels, before, (size_t)count * sizeof pixels[0]) != 0;
}

static void draw_twice(const Theme *theme, AppUsbStatus status, UsbScene *scene,
                       void (*change)(UsbScene *scene))
{
    Canvas canvas = canvas_for(theme);
    HalBattery battery = {.known = true, .percent = 64, .usb_power = true};
    status.scene = scene;
    usb_screen_draw(&canvas, theme, battery, status);
    memcpy(before, pixels, sizeof pixels);
    change(scene);
    usb_screen_draw(&canvas, theme, battery, status);
}

static void turn_reels(UsbScene *scene)
{
    scene->turn = (uint8_t)(scene->turn + 9);
}

static void storage_motion_redraws_only_the_cassette_square(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = medium(orientation);
        UsbScene scene = storage_scene(USB_SCENE_WRITING);
        AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVE);
        draw_twice(&theme, status, &scene, turn_reels);
        CHECK(changed(&theme));
        CHECK(changes_stay_inside(&theme, usb_screen_layout(&theme, status).figure));
    }
}

static void lower_levels(UsbScene *scene)
{
    scene->dac.level[0] = 20;
    scene->dac.level[1] = 255;
}

static void the_level_meter_redraws_only_its_own_area(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = medium(orientation);
        UsbScene scene = dac_scene(USB_SCENE_DAC_PLAYING);
        AppUsbStatus status = {.view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT)};
        draw_twice(&theme, status, &scene, lower_levels);
        CHECK(changed(&theme));
        CHECK(changes_stay_inside(&theme, usb_screen_layout(&theme, status).meter));
    }
}

static void keep_still(UsbScene *scene)
{
    (void)scene;
}

static void a_still_scene_does_not_follow_the_clock(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = storage_scene(USB_SCENE_CONNECTED);
    fake_hal_set_milliseconds(0);
    Canvas canvas = canvas_for(&theme);
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVE);
    status.scene = &scene;
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 50}, status);
    memcpy(before, pixels, sizeof pixels);
    fake_hal_set_milliseconds(5000);
    draw_twice(&theme, status, &scene, keep_still);
    CHECK(!changed(&theme));
}

static void drive_sizes_copies_and_ejected_drives_are_shown(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    scene.drives[1].state = USB_SCENE_DRIVE_EJECTED;
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVE);
    status.scene = &scene;
    Canvas canvas = canvas_for(&theme);
    text_spy_reset();
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
    CHECK(saw(i18n_text(STR_USB_STATE_WRITING)));
    CHECK(saw("4.2 GB"));
    CHECK(saw(i18n_text(STR_USB_COPIED)));
    CHECK(saw("13.0 MB"));
    CHECK(saw(i18n_text(STR_USB_EJECTED)));
    CHECK(saw(i18n_text(STR_USB_HIGH_SPEED)));
    CHECK(saw("64%"));
}

static void dac_details_show_what_the_session_measured(void)
{
    Theme theme = theme_for(APPEARANCE_GRAPHITE, ORIENTATION_PORTRAIT, TEXT_SIZE_MEDIUM);
    UsbScene scene = dac_scene(USB_SCENE_DAC_PLAYING);
    AppUsbStatus status = {.view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT)};
    status.scene = &scene;
    Canvas canvas = canvas_for(&theme);
    text_spy_reset();
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
    CHECK(saw("44.1 kHz · 24-bit"));
    CHECK(saw("-12 dB"));
    CHECK(saw(i18n_text(STR_JACK_35)));
    CHECK(saw("20 ms"));
    CHECK(saw("+12 ppm"));
    CHECK(saw("12:34"));
    scene.dac.host_muted = true;
    scene.dac.seconds = 3725;
    text_spy_reset();
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
    CHECK(saw(i18n_text(STR_USB_DAC_MUTED_VALUE)));
    CHECK(saw("1:02:05"));
}

static void count_service(void)
{
    registered_draws++;
}

static void rendering_service_preserves_the_complete_dac_frame(void)
{
    Theme theme = theme_for(APPEARANCE_GRAPHITE, ORIENTATION_LANDSCAPE, TEXT_SIZE_MEDIUM);
    UsbScene scene = dac_scene(USB_SCENE_DAC_PLAYING);
    AppUsbStatus status = {
        .view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT), .scene = &scene};
    Canvas canvas = canvas_for(&theme);
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
    memcpy(before, pixels, sizeof pixels);
    registered_draws = 0;
    scene.service = count_service;
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
    CHECK(registered_draws > 8);
    CHECK_EQUAL(0, memcmp(before, pixels, sizeof pixels));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(rendering_service_preserves_the_complete_dac_frame),
        TEST_CASE(every_usb_state_fits_every_theme_size_and_orientation),
        TEST_CASE(connecting_uses_the_overlay_and_a_cleared_renderer_the_resident_fallback),
        TEST_CASE(partial_redraws_are_split_into_bounded_horizontal_bands),
        TEST_CASE(storage_motion_redraws_only_the_cassette_square),
        TEST_CASE(the_level_meter_redraws_only_its_own_area),
        TEST_CASE(a_still_scene_does_not_follow_the_clock),
        TEST_CASE(drive_sizes_copies_and_ejected_drives_are_shown),
        TEST_CASE(dac_details_show_what_the_session_measured),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
