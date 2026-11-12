#include <stdio.h>
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
#include "screens/usb_screen_parts.h"
#include "text_spy.h"
#include "ui/label.h"
#include "ui/wire_motion.h"
#ifdef ECHO_TEST_GLYPHS
#include "sim_glyphs.h"
#endif

enum {
    THEMES = APPEARANCE_CLASSIC + 1,
    GIB_SECTORS = 2097152,
    DAC_RATE = 44100,
    STORAGE_DETAIL_PAGES = 2,
    DAC_DETAIL_PAGES = 2,
    VARIANTS = 24
};

typedef struct Variant {
    AppUsbStatus status;
    UsbScene scene;
    const char *name;
} Variant;

static char paged_hint[96];

static AppUsbStatus with_page_hint(const Theme *theme, AppUsbStatus status)
{
    StringId frame = status.view == APP_USB_DAC ? STR_USB_DAC_PAGE_HINT : STR_USB_DRIVES_HINT;
    StringBuilder builder = string_builder_make(paged_hint, sizeof paged_hint);
    i18n_format_text(&builder, frame, i18n_text(STR_USB_BACK_OVERVIEW));
    AppUsbStatus probe = status;
    probe.detail = paged_hint;
    int pages = usb_screen_pages(theme, probe);
    uint8_t page = usb_screen_scene(status)->page;
    if (pages <= 1 || usb_screen_scene(status)->menu != NULL) {
        return status;
    }
    StringId action = page + 1 >= pages ? STR_USB_BACK_OVERVIEW
                      : page == 0       ? STR_USB_BACK_DETAILS
                                        : STR_USB_BACK_MORE;
    builder = string_builder_make(paged_hint, sizeof paged_hint);
    i18n_format_text(&builder, frame, i18n_text(action));
    status.detail = paged_hint;
    return status;
}

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

static const uint16_t WRITE_RATES[USB_METER_SAMPLES] = {
    0,     0,     1200,  8500,  21000, 30500, 34800, 36100, 35200, 36900,
    37400, 36000, 35800, 37200, 36500, 38200, 37000, 36400, 37900, 38400};
static const uint16_t READ_RATES[USB_METER_SAMPLES] = {0, 0, 2600, 900, 0, 0, 0, 300, 0,   0,
                                                       0, 0, 0,    450, 0, 0, 0, 0,   120, 0};

static void fill_meter(UsbMeter *meter, bool busy, bool reading)
{
    uint32_t totals[USB_METER_DIRECTIONS] = {0, 0};
    const uint16_t *read = reading ? WRITE_RATES : READ_RATES;
    const uint16_t *written = reading ? READ_RATES : WRITE_RATES;
    usb_meter_start(meter, 0);
    for (uint32_t index = 0; index < USB_METER_SAMPLES; index++) {
        bool quiet = !busy && index + 1 == USB_METER_SAMPLES;
        totals[USB_METER_READ] += quiet ? 0 : read[index] * 1000u / 1024u;
        totals[USB_METER_WRITE] += quiet ? 0 : written[index] * 1000u / 1024u;
        usb_meter_sample(meter, totals, (index + 1) * USB_METER_PERIOD_MS);
    }
}

static UsbScene storage_scene(uint8_t state)
{
    UsbScene scene = {
        .state = state,
        .high_speed = true,
        .written_kib = 12700,
        .link = {.seconds = 754, .packet = 512, .busy_percent = 62},
        .drives = {{4 * GIB_SECTORS, USB_SCENE_DRIVE_MOUNTED},
                   {60 * GIB_SECTORS, USB_SCENE_DRIVE_MOUNTED}},
        .usage = {
            {.free_sectors = GIB_SECTORS + GIB_SECTORS / 8,
             .read_sectors = 4750,
             .written_sectors = 25400,
             .reads = 812,
             .writes = 164,
             .active = true},
            {.free_sectors = 38 * GIB_SECTORS + GIB_SECTORS / 3, .read_sectors = 96, .reads = 12}}};
    bool busy = state == USB_SCENE_WRITING || state == USB_SCENE_READING;
    fill_meter(&scene.meter, busy, state == USB_SCENE_READING);
    scene.busy = busy;
    scene.lit[USB_METER_READ] = state == USB_SCENE_READING;
    scene.lit[USB_METER_WRITE] = state == USB_SCENE_WRITING;
    scene.rate = busy ? usb_meter_total(&scene.meter, 0) : 0;
    scene.usage[0].write_glow = state == USB_SCENE_WRITING ? 255 : 0;
    scene.usage[0].read_glow = state == USB_SCENE_READING ? 255 : 40;
    return scene;
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
                                  .level = {200, 150},
                                  .channels = 2,
                                  .alt = 2,
                                  .gain_register = 0x10,
                                  .asynchronous = true,
                                  .buffer_low_ms = 17,
                                  .buffer_high_ms = 23,
                                  .buffer_average_ms = 20,
                                  .feedback = 362005,
                                  .data_rate = 265};
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
    list[count] = list[1];
    list[count].scene.drives[1] = (UsbSceneDrive){.state = USB_SCENE_DRIVE_ABSENT};
    list[count].scene.usage[1] = (UsbSceneUsage){0};
    list[count++].name = "storage-no-card";
    list[count] = list[1];
    usb_meter_start(&list[count].scene.meter, 0);
    list[count].scene.usage[0].active = false;
    list[count++].name = "storage-resting";
    for (uint8_t page = 1; page <= STORAGE_DETAIL_PAGES; page++) {
        list[count] = list[3];
        list[count].scene.page = page;
        list[count++].name = page == 1 ? "storage-details" : "storage-details-2";
    }
    list[count] = list[3];
    list[count].scene.lit[USB_METER_READ] = true;
    list[count].scene.usage[0].read_glow = 255;
    list[count++].name = "storage-both";
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
    for (uint8_t page = 1; page <= DAC_DETAIL_PAGES; page++) {
        list[count] = list[count - 3 - (page - 1)];
        list[count].scene.page = page;
        list[count++].name = page == 1 ? "dac-details" : "dac-details-2";
    }
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

static void check_texts(const Variant *variant, AppUsbStatus status)
{
    bool connected =
        variant->status.view == APP_USB_STORAGE && usb_screen_shows_traffic(&variant->scene);
    CHECK(saw(connected ? i18n_text(STR_USB_STATE_CONNECTED)
                        : charge_screen_usb_title(variant->status.view)));
    CHECK(saw_start(status.detail));
    bool overview = variant->scene.page == USB_SCENE_OVERVIEW;
    bool storage = variant->status.view == APP_USB_STORAGE;
    if (storage && variant->scene.state <= USB_SCENE_LEAVING && variant->scene.state > 0) {
        bool traffic = usb_screen_shows_traffic(&variant->scene);
        if (overview || !traffic) {
            CHECK(saw(i18n_text(traffic ? STR_USB_STAT_INTERNAL : STR_POWER_INTERNAL)));
        }
    }
    if (variant->scene.state == USB_SCENE_DAC_PLAYING && variant->scene.menu == NULL && overview) {
        CHECK(saw(i18n_text(STR_PLAYING)));
        CHECK(saw("44.1 kHz · 24-bit"));
        CHECK(saw(i18n_text(STR_USB_DAC_HOST)));
    }
}

static void every_usb_state_fits_every_theme_size_and_orientation(void)
{
#ifdef ECHO_TEST_GLYPHS
    sim_glyphs_attach();
#endif
    Variant list[VARIANTS];
    int count = variants(list);
    CHECK(count <= VARIANTS);
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
                    if (list[index].scene.page >= usb_screen_pages(&theme, status)) {
                        continue;
                    }
                    status = with_page_hint(&theme, status);
                    layout_holds_together(&theme, status);
                    text_spy_reset();
                    usb_screen_draw(
                        &canvas, &theme,
                        (HalBattery){
                            .known = true, .percent = 64, .usb_power = true, .charging = true},
                        status);
                    check_texts(&list[index], status);
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
        UsbScene scene = storage_scene(USB_SCENE_WAITING);
        AppUsbStatus status = storage_status(STR_USB_WAITING_HINT);
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

static void rates_read_in_tenths_of_a_megabyte(void)
{
    char text[USB_SCREEN_NUMBER_CAPACITY];
    usb_screen_rate_text(text, 38400);
    CHECK(strcmp(text, "38.4") == 0);
    usb_screen_rate_text(text, 49);
    CHECK(strcmp(text, "0.0") == 0);
    usb_screen_rate_text(text, 950);
    CHECK(strcmp(text, "1.0") == 0);
}

static void draw_text_of(const Theme *theme, AppUsbStatus status, UsbScene *scene)
{
    status.scene = scene;
    Canvas canvas = canvas_for(theme);
    text_spy_reset();
    usb_screen_draw(&canvas, theme, (HalBattery){.known = true, .percent = 64}, status);
}

static void the_overview_shows_the_rate_drives_and_their_free_space(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    scene.drives[1].state = USB_SCENE_DRIVE_EJECTED;
    draw_text_of(&theme, storage_status(STR_USB_EJECT_DRIVES), &scene);
    CHECK(saw(i18n_text(STR_USB_RATE_UNIT)));
    CHECK(!saw(i18n_text(STR_USB_STATE_WRITING)));
    CHECK(saw("Read 2.1 MB · Written 287 MB"));
    CHECK(saw(i18n_text(STR_USB_STAT_INTERNAL)));
    CHECK(saw("1.2 GB free"));
    CHECK(saw(i18n_text(STR_USB_EJECTED)));
    CHECK(saw("64%"));
}

static void an_idle_overview_shows_the_session_totals(void)
{
    Theme theme = medium(ORIENTATION_PORTRAIT);
    UsbScene scene = storage_scene(USB_SCENE_CONNECTED);
    scene.drives[1] = (UsbSceneDrive){.state = USB_SCENE_DRIVE_ABSENT};
    scene.usage[1] = (UsbSceneUsage){0};
    scene.usage[0].free_sectors = USB_SCENE_FREE_UNKNOWN;
    draw_text_of(&theme, storage_status(STR_USB_EJECT_DRIVE), &scene);
    CHECK(saw(i18n_text(STR_USB_RATE_UNIT)));
    CHECK(saw(i18n_text(STR_USB_STAT_READ)));
    CHECK(saw("2.1 MB"));
    CHECK(saw("268 MB"));
    CHECK(saw(i18n_text(STR_USB_NO_CARD)));
    CHECK(saw("4.2 GB total"));
}

static void storage_details_show_each_drive_and_the_link(void)
{
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    scene.link.errors = 2;
    scene.link.sense_key = 0x05;
    scene.link.sense_code = 0x20;
    Theme theme = medium(ORIENTATION_PORTRAIT);
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    status.scene = &scene;
    CHECK_EQUAL(1 + STORAGE_DETAIL_PAGES, usb_screen_pages(&theme, status));
    scene.page = 1;
    draw_text_of(&theme, status, &scene);
    CHECK(saw("13.0 MB"));
    CHECK(saw("812 / 164"));
    CHECK(saw("16 KB"));
    CHECK(saw("1.2 GB"));
    CHECK(saw("41.1 GB"));
    scene.page = 2;
    draw_text_of(&theme, status, &scene);
    CHECK(saw("38.4 MB/s"));
    CHECK(saw("62%"));
    CHECK(saw("2 · last 05/20"));
    CHECK(saw("480 Mbit/s · 512 B"));
    CHECK(saw("12:34"));
}

static void landscape_details_put_the_drives_side_by_side(void)
{
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    status.scene = &scene;
    CHECK_EQUAL(1 + STORAGE_DETAIL_PAGES, usb_screen_pages(&theme, status));
    scene.page = 1;
    draw_text_of(&theme, status, &scene);
    CHECK(saw(i18n_text(STR_USB_STAT_INTERNAL)));
    CHECK(saw(i18n_text(STR_USB_STAT_CARD)));
    CHECK(!saw(i18n_text(STR_USB_STAT_PEAK)));
    scene.page = 2;
    draw_text_of(&theme, status, &scene);
    CHECK(saw(i18n_text(STR_USB_STAT_PEAK)));
}

static void a_missing_card_drops_its_rows(void)
{
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    scene.drives[1] = (UsbSceneDrive){.state = USB_SCENE_DRIVE_ABSENT};
    scene.usage[1] = (UsbSceneUsage){0};
    Theme theme = medium(ORIENTATION_PORTRAIT);
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVE);
    status.scene = &scene;
    CHECK_EQUAL(2, usb_screen_pages(&theme, status));
    scene.page = 1;
    draw_text_of(&theme, status, &scene);
    CHECK(saw(i18n_text(STR_USB_NO_CARD)));
    CHECK(saw(i18n_text(STR_USB_STAT_PEAK)));
}

static bool has_digit(const char *text)
{
    for (; *text != '\0'; text++) {
        if (*text >= '0' && *text <= '9') {
            return true;
        }
    }
    return false;
}

static int words_of(char words[][64], int capacity)
{
    int count = 0;
    for (int index = 0; index < text_spy_count() && count < capacity; index++) {
        const char *shown = text_spy_entry(index)->shown;
        if (!has_digit(shown)) {
            snprintf(words[count++], 64, "%s", shown);
        }
    }
    return count;
}

static void copy_frame(UsbScene *scene, int frame)
{
    static const uint8_t WRITE_GLOWS[] = {255, 96, 224};
    static const uint8_t CARD_GLOWS[] = {0, 160, 32};
    uint32_t totals[USB_METER_DIRECTIONS] = {scene->meter.sectors[USB_METER_READ],
                                             scene->meter.sectors[USB_METER_WRITE] +
                                                 (uint32_t)(36000 + 2500 * frame)};
    usb_meter_sample(&scene->meter, totals, scene->meter.sampled_ms + USB_METER_PERIOD_MS);
    scene->usage[0].write_glow = WRITE_GLOWS[frame];
    scene->usage[1].write_glow = CARD_GLOWS[frame];
    scene->usage[0].written_sectors += 36000;
}

static void a_copy_keeps_its_words_still_while_the_lights_and_rates_move(void)
{
    static char first[24][64];
    static char later[24][64];
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = theme_for(APPEARANCE_NEON_NOIR, orientation, TEXT_SIZE_MEDIUM);
        UsbScene scene = storage_scene(USB_SCENE_WRITING);
        AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
        status.scene = &scene;
        status = with_page_hint(&theme, status);
        Canvas canvas = canvas_for(&theme);
        int words = 0;
        for (int frame = 0; frame < 3; frame++) {
            copy_frame(&scene, frame);
            text_spy_reset();
            usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
            screen_snapshot_write(&canvas, "copy-frame", orientation * 10 + frame + 1);
            int count = words_of(frame == 0 ? first : later, 24);
            words = frame == 0 ? count : words;
            CHECK_EQUAL(words, count);
            for (int index = 0; frame > 0 && index < count; index++) {
                CHECK(strcmp(first[index], later[index]) == 0);
            }
        }
    }
}

static void the_totals_move_with_the_rate_from_the_same_counters(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    scene.usage[0] = (UsbSceneUsage){0};
    scene.usage[1] = (UsbSceneUsage){0};
    usb_meter_start(&scene.meter, 0);
    uint32_t totals[USB_METER_DIRECTIONS] = {0, 0};
    for (uint32_t sample = 1; sample <= 3; sample++) {
        totals[USB_METER_WRITE] += 2048;
        usb_meter_sample(&scene.meter, totals, sample * USB_METER_PERIOD_MS);
        draw_text_of(&theme, storage_status(STR_USB_EJECT_DRIVES), &scene);
        char expected[USB_SCREEN_TEXT_CAPACITY];
        snprintf(expected, sizeof expected, "Read 0.0 MB · Written %u.%u MB",
                 (unsigned)(sample * 10486 / 10000), (unsigned)(sample * 10486 / 1000 % 10));
        CHECK(saw(expected));
        CHECK(usb_meter_total(&scene.meter, 0) > 0);
    }
}

static void a_total_that_moved_never_reads_zero(void)
{
    char text[USB_SCREEN_TEXT_CAPACITY];
    usb_screen_size_text(text, 0);
    CHECK(strcmp(text, "0.0 MB") == 0);
    usb_screen_size_text(text, 1);
    CHECK(strcmp(text, "0.1 MB") == 0);
    usb_screen_size_text(text, 196);
    CHECK(strcmp(text, "0.1 MB") == 0);
}

static void light_writing(UsbScene *scene)
{
    scene->lit[USB_METER_WRITE] = true;
    scene->rate = USB_METER_RATE_LIMIT;
}

static void light_both(UsbScene *scene)
{
    light_writing(scene);
    scene->lit[USB_METER_READ] = true;
    scene->rate = 1200;
}

static UsbScene resting_scene(void)
{
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    scene.lit[USB_METER_WRITE] = false;
    scene.rate = 0;
    return scene;
}

static void activity_stays_in_its_row(const Theme *theme, void (*change)(UsbScene *scene))
{
    UsbScene scene = resting_scene();
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    draw_twice(theme, status, &scene, change);
    CHECK(changed(theme));
    status.scene = &scene;
    Rect row = usb_screen_activity(theme, status);
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    Rect inside = rect_intersection(usb_screen_column(&layout), row);
    CHECK(inside.x == row.x && inside.y == row.y && inside.width == row.width &&
          inside.height == row.height);
    CHECK(row.height > font_line_height(theme->fonts.title));
    CHECK(changes_stay_inside(theme, row));
}

static void lit_arrows_and_the_rate_change_only_the_activity_row(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(appearance, orientation, size);
                activity_stays_in_its_row(&theme, light_writing);
                activity_stays_in_its_row(&theme, light_both);
            }
        }
    }
}

static int pixels_of(const Theme *theme, Rect area, Color color)
{
    int found = 0;
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            found += pixels[y * theme->metrics.screen_width + x] == color;
        }
    }
    return found;
}

static void a_resting_row_dims_both_arrows_and_a_lit_one_takes_its_colour(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = resting_scene();
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    status.scene = &scene;
    Rect row = usb_screen_activity(&theme, status);
    draw_text_of(&theme, status, &scene);
    CHECK(pixels_of(&theme, row, theme.colors.text_tertiary) > 0);
    CHECK_EQUAL(0, pixels_of(&theme, row, theme.colors.success));
    CHECK_EQUAL(0, pixels_of(&theme, row, theme.colors.accent));
    CHECK(saw(i18n_text(STR_USB_RATE_UNIT)));
    light_writing(&scene);
    draw_text_of(&theme, status, &scene);
    CHECK(pixels_of(&theme, row, theme.colors.success) > 0);
    CHECK_EQUAL(0, pixels_of(&theme, row, theme.colors.accent));
    light_both(&scene);
    draw_text_of(&theme, status, &scene);
    CHECK(pixels_of(&theme, row, theme.colors.accent) > 0);
}

static Color drive_led(const Theme *theme, UsbScene *scene, int lun)
{
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    status.scene = scene;
    draw_text_of(theme, status, scene);
    Rect lights = usb_screen_drive_lights(theme, status, lun);
    return pixels[(lights.y + lights.height / 2) * theme->metrics.screen_width + lights.x + 1];
}

static void a_drive_light_stays_lit_while_the_host_uses_the_drive_and_never_flashes(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = medium(orientation);
        UsbScene scene = storage_scene(USB_SCENE_WRITING);
        scene.usage[0].write_glow = 0;
        scene.usage[0].read_glow = 0;
        CHECK_EQUAL(theme.colors.success, drive_led(&theme, &scene, 0));
        scene.usage[0].write_glow = 255;
        CHECK_EQUAL(theme.colors.success, drive_led(&theme, &scene, 0));
        scene.usage[0].reads = 0;
        scene.usage[0].writes = 0;
        CHECK(drive_led(&theme, &scene, 0) != theme.colors.success);
        scene = storage_scene(USB_SCENE_WRITING);
        scene.drives[1].state = USB_SCENE_DRIVE_EJECTED;
        CHECK(drive_led(&theme, &scene, 1) != theme.colors.success);
        scene.drives[1] = (UsbSceneDrive){.state = USB_SCENE_DRIVE_ABSENT};
        CHECK(drive_led(&theme, &scene, 1) != theme.colors.success);
    }
}

static Color fresh[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];

static void flatten(UsbScene *scene)
{
    uint32_t totals[USB_METER_DIRECTIONS] = {scene->meter.sectors[USB_METER_READ],
                                             scene->meter.sectors[USB_METER_WRITE]};
    usb_meter_start(&scene->meter, 0);
    scene->meter.sectors[USB_METER_READ] = totals[USB_METER_READ];
    scene->meter.sectors[USB_METER_WRITE] = totals[USB_METER_WRITE];
    for (uint32_t sample = 1; sample <= USB_METER_SAMPLES; sample++) {
        usb_meter_sample(&scene->meter, totals, sample * USB_METER_PERIOD_MS);
    }
}

static void graph_redraw_leaves_nothing_of_the_frame_before(const Theme *theme)
{
    HalBattery battery = {.known = true, .percent = 64};
    UsbScene scene = storage_scene(USB_SCENE_READING);
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    status.scene = &scene;
    Canvas canvas = canvas_for(theme);
    usb_screen_draw(&canvas, theme, battery, status);
    flatten(&scene);
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    canvas.clip = usb_screen_graph(theme, &layout);
    usb_screen_draw(&canvas, theme, battery, status);
    memcpy(before, pixels, sizeof pixels);
    canvas = canvas_for(theme);
    usb_screen_draw(&canvas, theme, battery, status);
    memcpy(fresh, pixels, sizeof pixels);
    int count = theme->metrics.screen_width * theme->metrics.screen_height;
    CHECK_EQUAL(0, memcmp(before, fresh, (size_t)count * sizeof pixels[0]));
}

static void a_low_graph_frame_leaves_no_line_of_a_high_one(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
                Theme theme = theme_for(appearance, orientation, size);
                graph_redraw_leaves_nothing_of_the_frame_before(&theme);
            }
        }
    }
}

static void flat_rows_lie_on_the_floor_instead_of_drawing_ridge_lines(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = medium(orientation);
        UsbScene scene = storage_scene(USB_SCENE_WRITING);
        flatten(&scene);
        AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
        status.scene = &scene;
        draw_text_of(&theme, status, &scene);
        UsbScreenLayout layout = usb_screen_layout(&theme, status);
        Rect graph = usb_screen_graph(&theme, &layout);
        CHECK_EQUAL(0, pixels_of(&theme, graph, theme.colors.accent));
        CHECK_EQUAL(0, pixels_of(&theme, graph, theme.colors.success));
    }
}

static int rate_ink(const Theme *theme, UsbScene *scene)
{
    AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
    status.scene = scene;
    draw_text_of(theme, status, scene);
    return pixels_of(theme, usb_screen_activity(theme, status), theme->colors.text);
}

static void the_rate_keeps_its_colour_between_commands_and_dims_only_when_the_session_rests(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    int lit = rate_ink(&theme, &scene);
    CHECK(lit > 0);
    scene.lit[USB_METER_WRITE] = false;
    CHECK_EQUAL(lit, rate_ink(&theme, &scene));
    scene.busy = false;
    scene.lit[USB_METER_WRITE] = true;
    CHECK_EQUAL(0, rate_ink(&theme, &scene));
}

static void speed_shot(const Theme *theme, AppUsbStatus status, UsbScene *scene, int shot)
{
    status.scene = scene;
    Canvas canvas = canvas_for(theme);
    text_spy_reset();
    usb_screen_draw(&canvas, theme, (HalBattery){.known = true, .percent = 64}, status);
    screen_snapshot_write(&canvas, "speed", shot);
}

static void the_header_tags_the_link_speed_and_a_fallback(void)
{
    static const char *const TAGS[] = {"HS", "FS", "FS!"};
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = medium(orientation);
        for (int kind = 0; kind < 3; kind++) {
            UsbScene scene = storage_scene(USB_SCENE_WRITING);
            scene.high_speed = kind == 0;
            scene.fell_back = kind == 2;
            speed_shot(&theme, storage_status(STR_USB_EJECT_DRIVES), &scene,
                       orientation * 10 + kind);
            CHECK(saw(TAGS[kind]));
            CHECK(saw(i18n_text(STR_USB_STATE_CONNECTED)));
            CHECK(!saw(charge_screen_usb_title(APP_USB_STORAGE)));
            scene = dac_scene(USB_SCENE_DAC_PLAYING);
            scene.high_speed = kind == 0;
            scene.fell_back = kind == 2;
            AppUsbStatus dac = {.view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT)};
            speed_shot(&theme, dac, &scene, orientation * 10 + kind + 5);
            CHECK(saw(TAGS[kind]));
            CHECK(saw(charge_screen_usb_title(APP_USB_DAC)));
        }
        UsbScene waiting = storage_scene(USB_SCENE_WAITING);
        speed_shot(&theme, storage_status(STR_USB_KEEP_HOLDING), &waiting, orientation * 10 + 9);
        CHECK(!saw("HS"));
        CHECK(saw(charge_screen_usb_title(APP_USB_STORAGE)));
    }
}

static bool rows_plain(const Theme *theme, Rect area)
{
    for (int y = area.y; y < area.y + area.height; y++) {
        const Color *row = &pixels[y * theme->metrics.screen_width];
        for (int x = area.x + 1; x < area.x + area.width; x++) {
            if (row[x] != row[area.x]) {
                return false;
            }
        }
    }
    return true;
}

static void no_backdrop_grid_shows_around_the_terrain(void)
{
    for (int appearance = 0; appearance < THEMES; appearance++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            Theme theme = theme_for(appearance, orientation, TEXT_SIZE_MEDIUM);
            UsbScene scene = storage_scene(USB_SCENE_WRITING);
            AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
            status.scene = &scene;
            draw_text_of(&theme, status, &scene);
            UsbScreenLayout layout = usb_screen_layout(&theme, status);
            Rect graph = usb_screen_graph(&theme, &layout);
            int below = graph.y + graph.height;
            CHECK(below < layout.art.y + layout.art.height);
            CHECK(rows_plain(&theme, (Rect){layout.art.x, below, layout.art.width,
                                            layout.art.y + layout.art.height - below}));
        }
    }
}

static void a_page_past_the_last_shows_the_overview(void)
{
    UsbScene scene = storage_scene(USB_SCENE_WRITING);
    Theme theme = medium(ORIENTATION_PORTRAIT);
    scene.page = 9;
    draw_text_of(&theme, storage_status(STR_USB_EJECT_DRIVES), &scene);
    CHECK(saw(i18n_text(STR_USB_RATE_UNIT)));
}

static void warnings_and_waits_have_no_details(void)
{
    Theme theme = medium(ORIENTATION_LANDSCAPE);
    UsbScene scene = storage_scene(USB_SCENE_WAITING);
    AppUsbStatus status = storage_status(STR_USB_WAITING_HINT);
    status.scene = &scene;
    CHECK_EQUAL(1, usb_screen_pages(&theme, status));
    scene = dac_scene(USB_SCENE_DAC_PLAYING);
    scene.menu = &menu.view;
    status.view = APP_USB_DAC;
    CHECK_EQUAL(1, usb_screen_pages(&theme, status));
}

static void age_an_old_sample(UsbScene *scene)
{
    scene->meter.history[USB_METER_WRITE][(scene->meter.newest + 15) % USB_METER_SAMPLES] = 9000;
}

static void a_new_sample_changes_only_the_graph(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = medium(orientation);
        UsbScene scene = storage_scene(USB_SCENE_WRITING);
        AppUsbStatus status = storage_status(STR_USB_EJECT_DRIVES);
        draw_twice(&theme, status, &scene, age_an_old_sample);
        CHECK(changed(&theme));
        status.scene = &scene;
        UsbScreenLayout layout = usb_screen_layout(&theme, status);
        CHECK(changes_stay_inside(&theme, usb_screen_graph(&theme, &layout)));
    }
}

static void dac_overview_shows_format_output_and_host(void)
{
    Theme theme = theme_for(APPEARANCE_GRAPHITE, ORIENTATION_PORTRAIT, TEXT_SIZE_MEDIUM);
    UsbScene scene = dac_scene(USB_SCENE_DAC_PLAYING);
    AppUsbStatus status = {.view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT)};
    draw_text_of(&theme, status, &scene);
    CHECK(saw("44.1 kHz · 24-bit"));
    CHECK(saw("-12 dB"));
    CHECK(saw(i18n_text(STR_JACK_35)));
    CHECK(saw("35"));
    CHECK(saw("20 ms"));
    scene.dac.host_muted = true;
    draw_text_of(&theme, status, &scene);
    CHECK(saw(i18n_text(STR_USB_DAC_MUTED_VALUE)));
}

static void dac_details_show_what_the_session_measured(void)
{
    Theme theme = theme_for(APPEARANCE_GRAPHITE, ORIENTATION_PORTRAIT, TEXT_SIZE_MEDIUM);
    UsbScene scene = dac_scene(USB_SCENE_DAC_PLAYING);
    AppUsbStatus status = {.view = APP_USB_DAC, .detail = i18n_text(STR_USB_DAC_HINT)};
    status.scene = &scene;
    CHECK_EQUAL(2, usb_screen_pages(&theme, status));
    scene.page = 1;
    scene.dac.host_volume = -12 * 256 - 128;
    draw_text_of(&theme, status, &scene);
    CHECK(saw("44100 Hz · 24-bit"));
    CHECK(saw("UAC2 alt 2 · 2 ch"));
    CHECK(saw("20 ms (17-23)"));
    CHECK(saw("+12 ppm"));
    CHECK(saw("5.5237"));
    CHECK(saw("Low · reg 0x10"));
    CHECK(saw("-12.5 dB"));
    CHECK(saw("265 kB/s"));
    CHECK(saw("12:34"));
    scene.dac.seconds = 3725;
    draw_text_of(&theme, status, &scene);
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
        TEST_CASE(rates_read_in_tenths_of_a_megabyte),
        TEST_CASE(the_overview_shows_the_rate_drives_and_their_free_space),
        TEST_CASE(an_idle_overview_shows_the_session_totals),
        TEST_CASE(storage_details_show_each_drive_and_the_link),
        TEST_CASE(landscape_details_put_the_drives_side_by_side),
        TEST_CASE(a_missing_card_drops_its_rows),
        TEST_CASE(a_page_past_the_last_shows_the_overview),
        TEST_CASE(no_backdrop_grid_shows_around_the_terrain),
        TEST_CASE(the_header_tags_the_link_speed_and_a_fallback),
        TEST_CASE(the_totals_move_with_the_rate_from_the_same_counters),
        TEST_CASE(a_total_that_moved_never_reads_zero),
        TEST_CASE(a_copy_keeps_its_words_still_while_the_lights_and_rates_move),
        TEST_CASE(warnings_and_waits_have_no_details),
        TEST_CASE(a_new_sample_changes_only_the_graph),
        TEST_CASE(a_low_graph_frame_leaves_no_line_of_a_high_one),
        TEST_CASE(flat_rows_lie_on_the_floor_instead_of_drawing_ridge_lines),
        TEST_CASE(the_rate_keeps_its_colour_between_commands_and_dims_only_when_the_session_rests),
        TEST_CASE(lit_arrows_and_the_rate_change_only_the_activity_row),
        TEST_CASE(a_resting_row_dims_both_arrows_and_a_lit_one_takes_its_colour),
        TEST_CASE(a_drive_light_stays_lit_while_the_host_uses_the_drive_and_never_flashes),
        TEST_CASE(dac_overview_shows_format_output_and_host),
        TEST_CASE(dac_details_show_what_the_session_measured),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
