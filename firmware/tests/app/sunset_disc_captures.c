#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "gfx/text.h"
#include "hal/display.h"
#include "hal/storage.h"
#include "headphone_store.h"
#include "music_check.h"
#include "music_record.h"
#include "screens/charge_screen.h"
#include "screens/usb_scene.h"
#include "screens/wire_boot.h"
#include "settings_store.h"
#include "sim_glyphs.h"
#include "ui/brand_mark.h"
#include "ui/header.h"
#include "ui/status_bar.h"

enum {
    SIDE = 480,
    START_MS = 1000,
    KEY_HOLD_MS = 30,
    SETTLE_MS = 700,
    THEMES = APPEARANCE_THEME_COUNT,
    GIB_SECTORS = 2097152
};

typedef struct Layout {
    int orientation;
    int size;
} Layout;

static const char *const THEME_NAMES[] = {"graphite", "neon-noir", "sunset", "sea-glass",
                                          "classic"};
static const char *const SIZE_NAMES[] = {"small", "medium", "large"};
static const char *const ORIENTATION_NAMES[] = {"landscape", "portrait"};
static const char *const STYLE_NAMES[] = {"cover", "cassette", "vu", "spectrum"};
static const char LONG_ALBUM[] = "This Won't Be The Last Time";

static Color pixels[SIDE * SIDE];
static const char *directory;
static uint32_t now_ms;
static Layout shown;

static void write_canvas(const Canvas *canvas, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", canvas->bounds.width, canvas->bounds.height);
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            Color color = canvas_pixel_at(canvas, x, y);
            uint8_t rgb[] = {(uint8_t)((color >> 11) * 255 / 31),
                             (uint8_t)((color >> 5 & 63) * 255 / 63),
                             (uint8_t)((color & 31) * 255 / 31)};
            fwrite(rgb, 1, sizeof rgb, file);
        }
    }
    fclose(file);
}

static Theme theme_of(int appearance, Layout layout, bool compact)
{
    Theme theme = BOARD_THEME;
    default_theme_set_appearance(&theme, (AppearanceTheme)appearance);
    default_theme_set_text_size(&theme, (TextSize)layout.size);
    default_theme_set_orientation(&theme, (Orientation)layout.orientation);
    default_theme_set_status_bar(&theme, compact);
    return theme;
}

static Canvas screen_canvas(const Theme *theme)
{
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    canvas_clear(&canvas, theme->colors.background);
    return canvas;
}

static void capture_icons(void)
{
    static const Color BACKS[] = {COLOR_RGB(22, 23, 27), COLOR_RGB(28, 20, 49),
                                  COLOR_RGB(57, 24, 47), COLOR_RGB(14, 43, 47),
                                  COLOR_RGB(36, 37, 34), COLOR_RGB(236, 225, 199)};
    for (int back = 0; back < 6; back++) {
        for (int kind = 0; kind < 4; kind++) {
            int size = kind < 2 ? 14 : 10;
            Canvas canvas = canvas_make(pixels, (Rect){0, 0, 18, 18});
            canvas_clear(&canvas, BACKS[back]);
            brand_mark_draw(
                &canvas, (BrandMark){{2, 2, size, size}, BACKS[back], UINT8_MAX, (kind & 1) != 0});
            char name[64];
            snprintf(name, sizeof name, "icon-%d-%d-%s", back, size, (kind & 1) ? "tile" : "bare");
            write_canvas(&canvas, name);
        }
    }
    for (int size = 32; size <= 128; size *= 2) {
        Canvas canvas = canvas_make(pixels, (Rect){0, 0, size, size});
        canvas_clear(&canvas, COLOR_RGB(236, 225, 199));
        brand_mark_draw(&canvas, (BrandMark){{0, 0, size, size}, 0, UINT8_MAX, true});
        char name[64];
        snprintf(name, sizeof name, "icon-large-%d", size);
        write_canvas(&canvas, name);
    }
}

static void capture_wire_boot(void)
{
    static const uint16_t STEPS[] = {64, 112, 160, 208, 256};
    static const int LOOKS[] = {APPEARANCE_GRAPHITE, APPEARANCE_CLASSIC};
    for (int look = 0; look < 2; look++) {
        for (int orientation = 0; orientation < 2; orientation++) {
            Theme theme = theme_of(LOOKS[look], (Layout){orientation, TEXT_SIZE_MEDIUM}, false);
            for (int step = 0; step < 5; step++) {
                Canvas canvas = screen_canvas(&theme);
                wire_boot_draw(&canvas, &theme, canvas.bounds,
                               (WireBootFrame){FIRMWARE_NAME, STEPS[step], false});
                char name[96];
                snprintf(name, sizeof name, "wire-%s-%s-%d", THEME_NAMES[LOOKS[look]],
                         ORIENTATION_NAMES[orientation], STEPS[step]);
                write_canvas(&canvas, name);
            }
        }
    }
}

static void capture_charge(int appearance, Layout layout)
{
    Theme theme = theme_of(appearance, layout, false);
    Canvas canvas = screen_canvas(&theme);
    HalBattery battery = {.known = true, .percent = 64, .usb_power = true, .charging = true};
    charge_screen_draw_at(&canvas, &theme, battery, 0);
    char name[96];
    snprintf(name, sizeof name, "charge-%s-%s-%s", THEME_NAMES[appearance],
             ORIENTATION_NAMES[layout.orientation], SIZE_NAMES[layout.size]);
    write_canvas(&canvas, name);
}

static void capture_usb(int appearance, Layout layout)
{
    Theme theme = theme_of(appearance, layout, false);
    Canvas canvas = screen_canvas(&theme);
    UsbScene scene = {.state = USB_SCENE_CONNECTED,
                      .high_speed = true,
                      .drives = {{4 * GIB_SECTORS, USB_SCENE_DRIVE_MOUNTED},
                                 {60 * GIB_SECTORS, USB_SCENE_DRIVE_MOUNTED}},
                      .usage = {{.free_sectors = GIB_SECTORS}, {.free_sectors = 38 * GIB_SECTORS}}};
    AppUsbStatus status = {.view = APP_USB_STORAGE,
                           .information = i18n_text(STR_POWER_INTERNAL_CARD),
                           .detail = i18n_text(STR_USB_EJECT_DRIVES),
                           .scene = &scene};
    usb_screen_draw(&canvas, &theme, (HalBattery){.known = true, .percent = 64}, status);
    char name[96];
    snprintf(name, sizeof name, "usb-%s-%s-%s", THEME_NAMES[appearance],
             ORIENTATION_NAMES[layout.orientation], SIZE_NAMES[layout.size]);
    write_canvas(&canvas, name);
}

static void step(void)
{
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void wait_ms(uint32_t duration_ms)
{
    uint32_t end = now_ms + duration_ms;
    while (now_ms < end) {
        now_ms += 16;
        step();
    }
}

static void tap(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + KEY_HOLD_MS);
    now_ms += KEY_HOLD_MS + 10;
    step();
}

static void hold(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    now_ms += 1000;
    step();
    fake_hal_queue_transition(key, false, now_ms);
    now_ms += 10;
    step();
}

static void down(int times)
{
    Key key = shown.orientation == ORIENTATION_LANDSCAPE ? KEY_VOLUME_UP : KEY_VOLUME_DOWN;
    for (int count = 0; count < times; count++) {
        tap(key);
    }
}

static void enter(void)
{
    tap(KEY_PLAY);
    wait_ms(SETTLE_MS);
}

static void back_home(void)
{
    hold(KEY_BACK);
    wait_ms(SETTLE_MS);
}

static void dump(const char *screen, const char *variant)
{
    const Theme *theme = app_theme();
    Canvas canvas = canvas_make(
        pixels, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    for (int y = 0; y < canvas.bounds.height; y++) {
        for (int x = 0; x < canvas.bounds.width; x++) {
            pixels[y * canvas.bounds.width + x] = fake_hal_pixel(x, y);
        }
    }
    char name[128];
    snprintf(name, sizeof name, "%s-%s-%s-%s", screen, variant,
             ORIENTATION_NAMES[shown.orientation], SIZE_NAMES[shown.size]);
    write_canvas(&canvas, name);
    printf("%s: %s\n", name, app_screen_title());
}

static void prefill_files(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    uint32_t covers_size = 0;
    const uint8_t *covers = fixture_covers_bytes(&covers_size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", bytes, size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/covers.bin", covers, covers_size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/build.tmp", NULL, 16 * 1024 * 1024);
    for (size_t index = 0; index < card_files_count(); index++) {
        HalFile file;
        if (!card_files_slow(index)) {
            continue;
        }
        if (hal_file_open(&file, card_files_path(index))) {
            hal_file_close(&file);
        } else {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
    fake_hal_add_file(MUSIC_CHECK_PATH, NULL, 4096);
    music_record_write(&(MusicRecord){.card = {.complete = true}});
    hal_internal_file_reserve(HEADPHONE_STORE_PATH, HEADPHONE_FILE_BYTES);
}

typedef struct Setup {
    int appearance;
    int style;
    bool everything;
    bool animation;
} Setup;

static void start_app(Layout layout, Setup setup)
{
    shown = layout;
    fake_hal_reset();
    sim_glyphs_attach();
    app_reset_for_test();
    prefill_files();
    fake_hal_set_battery((HalBattery){.known = true, .percent = 64, .millivolts = 3900});
    fake_hal_set_wall(1794390060u, 0);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    SettingsStore store;
    settings_store_load(&store);
    store.settings.orientation = (Orientation)layout.orientation;
    store.settings.text_size = (TextSize)layout.size;
    store.settings.appearance = (AppearanceTheme)setup.appearance;
    store.settings.startup_animation = setup.animation;
    if (setup.everything) {
        store.settings.battery_percent = true;
        store.settings.status_items =
            SETTINGS_STATUS_VOLUME | SETTINGS_STATUS_REPEAT | SETTINGS_STATUS_POSITION;
        store.settings.shuffle = SHUFFLE_SONGS;
        store.settings.repeat = REPEAT_ALL;
    }
    settings_set_now_playing_style(&store.settings, (NowPlayingStyle)setup.style);
    settings_store_save(&store);
    now_ms += 10;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    wait_ms(1500);
}

static void play_first_track(void)
{
    enter();
    down(4);
    enter();
    enter();
    enter();
}

static bool open_until(const char *title)
{
    for (int row = 0; row < 40; row++) {
        enter();
        if (strcmp(app_screen_title(), title) == 0) {
            return true;
        }
        tap(KEY_BACK);
        wait_ms(SETTLE_MS);
        down(1);
    }
    return false;
}

static void open_settings_row(const char *title)
{
    back_home();
    open_until("Settings");
    open_until(title);
}

static bool open_long_album(void)
{
    back_home();
    open_until("Music");
    open_until("Albums");
    return open_until(LONG_ALBUM);
}

static void capture_about(int appearance, Layout layout)
{
    start_app(layout, (Setup){appearance, NOW_PLAYING_COVER, false, false});
    open_settings_row("About");
    dump("about", THEME_NAMES[appearance]);
}

static void capture_header_sheet(Layout layout)
{
    start_app(layout, (Setup){APPEARANCE_GRAPHITE, NOW_PLAYING_COVER, true, false});
    play_first_track();
    dump("nowplaying", STYLE_NAMES[0]);
    tap(KEY_MENU);
    wait_ms(SETTLE_MS);
    dump("quicksettings", "graphite");
    back_home();
    dump("home", "graphite");
    open_until("Music");
    dump("music", "graphite");
    tap(KEY_BACK);
    wait_ms(120);
    dump("slide", "graphite");
    wait_ms(SETTLE_MS);
    open_settings_row("Screen timeout");
    dump("valuelist", "graphite");
    tap(KEY_BACK);
    wait_ms(SETTLE_MS);
    dump("settings", "graphite");
    wait_ms(2500);
    dump("settings", "idle");
    if (open_long_album()) {
        dump("longalbum", "graphite");
        wait_ms(2500);
        dump("longalbum", "scrolled");
    }
    back_home();
    open_until("Clock");
    dump("clock", "graphite");
}

static void capture_styles(Layout layout)
{
    for (int style = 1; style < 4; style++) {
        start_app(layout, (Setup){APPEARANCE_GRAPHITE, style, true, false});
        play_first_track();
        wait_ms(SETTLE_MS);
        dump("nowplaying", STYLE_NAMES[style]);
    }
}

static void capture_splash(Layout layout)
{
    static const uint32_t TIMES[] = {0, 100, 200, 300, 450};
    shown = layout;
    for (int look = 0; look < 2; look++) {
        int appearance = look == 0 ? APPEARANCE_GRAPHITE : APPEARANCE_CLASSIC;
        start_app(layout, (Setup){appearance, NOW_PLAYING_COVER, false, false});
        now_ms += 10;
        fake_hal_set_milliseconds(now_ms);
        app_start();
        app_show_splash();
        uint32_t started = now_ms;
        for (int index = 0; index < 5; index++) {
            now_ms = started + TIMES[index];
            step();
            char variant[32];
            snprintf(variant, sizeof variant, "%s-%u", THEME_NAMES[appearance],
                     (unsigned)TIMES[index]);
            dump("splash", variant);
        }
    }
}

static void capture_drops(Layout layout, bool compact)
{
    static const char *const TITLES[] = {"", "Music", "Settings", "Screen timeout",
                                         "A long album or settings title that needs room"};
    Theme theme = theme_of(APPEARANCE_GRAPHITE, layout, compact);
    int height = theme.metrics.header_height;
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, height * 5});
    canvas_clear(&canvas, theme.colors.background);
    for (int row = 0; row < 5; row++) {
        StatusBarState state = {.playback = STATUS_PLAYBACK_PLAYING,
                                .bluetooth = STATUS_BLUETOOTH_CONNECTED,
                                .battery_known = true,
                                .battery_percent = 64,
                                .locked = true,
                                .shuffle = STATUS_SHUFFLE_ALBUMS,
                                .percent = "64%",
                                .clock = "9:41",
                                .volume = "47",
                                .position = "3/12",
                                .repeat = 1,
                                .logo = true,
                                .title_width = text_width(theme.fonts.header, TITLES[row])};
        Rect room = status_bar_title_area(&theme, &state);
        theme.header_room = (HeaderRoom){(int16_t)room.x, (int16_t)(room.x + room.width), 0};
        Canvas band = canvas_make(pixels + row * height * theme.metrics.screen_width,
                                  (Rect){0, 0, theme.metrics.screen_width, height});
        header_draw(&band, &theme, TITLES[row][0] != '\0' ? TITLES[row] : NULL);
        status_bar_draw(&band, &theme, state);
    }
    char name[96];
    snprintf(name, sizeof name, "drops-%s-%s-%s", compact ? "compact" : "standard",
             ORIENTATION_NAMES[layout.orientation], SIZE_NAMES[layout.size]);
    write_canvas(&canvas, name);
}

int main(int argc, char **argv)
{
    directory = argc > 1 ? argv[1] : ".";
    capture_icons();
    capture_wire_boot();
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Layout layout = {orientation, size};
            for (int appearance = 0; appearance < THEMES; appearance++) {
                capture_charge(appearance, layout);
                capture_usb(appearance, layout);
                capture_about(appearance, layout);
            }
            capture_drops(layout, false);
            capture_drops(layout, true);
            capture_header_sheet(layout);
            capture_styles(layout);
            if (size == TEXT_SIZE_MEDIUM) {
                capture_splash(layout);
            }
        }
    }
    return 0;
}
