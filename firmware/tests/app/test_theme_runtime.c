#include <string.h>

#include "app_screens.h"
#include "check.h"
#include "core/hash.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/system.h"
#include "modules/modules.h"
#include "screens/listening_work.h"
#include "theme_picker.h"

static uint8_t first[540];
static uint8_t second[540];
static const char FIRST_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/themes/first.gtheme";
static const char SECOND_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/themes/second.gtheme";

static void sample(uint8_t *bytes, uint8_t id)
{
    memset(bytes, 0, sizeof first);
    memcpy(bytes, "DSTP", 4);
    store_put16(bytes + 4, 1);
    store_put16(bytes + 6, 512);
    store_put32(bytes + 8, sizeof first);
    bytes[16] = APPEARANCE_CLASSIC;
    bytes[18] = 20;
    bytes[19] = 1;
    bytes[20] = id;
    memcpy(bytes + 36, id == 1 ? "First" : "Second", id == 1 ? 6 : 7);
    memcpy(bytes + 68, "Maker", 6);
    memcpy(bytes + 100, "1.0.0", 6);
    memcpy(bytes + 132, "CC0-1.0", 8);
    for (int at = 0; at < 20; at++) {
        store_put16(bytes + 452 + at * 2, at == 0 ? 0 : UINT16_MAX);
    }
    store_put16(bytes + 512, THEME_ART_LIST);
    bytes[514] = 2;
    bytes[515] = 255;
    store_put16(bytes + 516, 3);
    store_put16(bytes + 518, 2);
    store_put32(bytes + 520, 528);
    store_put32(bytes + 524, 12);
    for (int at = 0; at < 6; at++) {
        store_put16(bytes + 528 + at * 2, (uint16_t)(100 + at));
    }
    uint32_t crc = theme_pack_crc(UINT32_MAX, bytes, 12);
    store_put32(bytes + 12, ~theme_pack_crc(crc, bytes + 16, sizeof first - 16));
}

static void reset(void)
{
    fake_hal_reset();
    sample(first, 1);
    sample(second, 2);
    fake_hal_add_file(FIRST_PATH, first, sizeof first);
    fake_hal_add_file(SECOND_PATH, second, sizeof second);
}

static void a_saved_pack_survives_restart_and_a_damaged_or_missing_pack_falls_back(void)
{
    reset();
    ThemeRuntime runtime = {0};
    theme_runtime_boot(&runtime);
    CHECK(!runtime.active);
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    CHECK(theme_runtime_save(&runtime, -1));
    theme_runtime_boot(&runtime);
    CHECK(runtime.active);
    CHECK_EQUAL(1, runtime.pack.id[0]);
    first[452] ^= 1;
    theme_runtime_boot(&runtime);
    CHECK(!runtime.active);
    CHECK_EQUAL(APPEARANCE_NEON_NOIR, runtime.shown_base);
    sample(first, 1);
    fake_hal_fail_file_opens(FIRST_PATH);
    theme_runtime_boot(&runtime);
    CHECK(!runtime.active);
    CHECK_EQUAL(APPEARANCE_NEON_NOIR, runtime.shown_base);
}

static void applying_a_pack_preserves_fonts_layout_and_all_palette_roles(void)
{
    reset();
    ThemeRuntime runtime = {0};
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    Theme theme = ECHO_THEME;
    theme_runtime_apply(&runtime, &theme);
    CHECK_EQUAL(runtime.pack.palette.colors.background, theme.colors.background);
    CHECK_EQUAL(runtime.pack.palette.colors.danger, theme.colors.danger);
    CHECK_EQUAL(runtime.pack.palette.colors.success, theme.colors.success);
    CHECK_EQUAL(runtime.pack.palette.colors.backdrop_bottom, theme.colors.backdrop_bottom);
    CHECK(theme.fonts.body == ECHO_THEME.fonts.body);
    CHECK_EQUAL(ECHO_THEME.metrics.row_height, theme.metrics.row_height);
    CHECK(theme.draw_art != NULL);
}

static void pack_art_draws_only_through_the_loaded_ui_module(void)
{
    reset();
    ThemeRuntime runtime = {0};
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    Theme theme = ECHO_THEME;
    theme_runtime_apply(&runtime, &theme);
    static Color pixels[480 * 222];
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 480, 222});
    Rect area = {0, 30, 480, 192};
    canvas_clear(&canvas, 0);
    CHECK(!theme_art_draw(&theme, &canvas, THEME_ART_LIST, area));
    CHECK_EQUAL(0u, fake_hal_loaded_module());
    CHECK_EQUAL(0, pixels[30 * 480]);
    CHECK(hal_system_load_module(SCREEN_MODULE_PLAYING) != NULL);
    CHECK(theme_art_draw(&theme, &canvas, THEME_ART_LIST, area));
    CHECK_EQUAL(100, pixels[30 * 480]);
    ThemeRuntime idle = {0};
    theme_runtime_apply(&idle, &theme);
    CHECK(theme.draw_art == NULL);
}

static void strips_and_clipped_rectangles_match_a_full_render(void)
{
    reset();
    ThemeRuntime runtime = {0};
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    static Color full[480 * 222];
    static Color strip[480 * 17];
    ThemeArtRequest request = {.area = {0, 30, 480, 192}, .slot = THEME_ART_LIST};
    Canvas canvas = canvas_make(full, (Rect){0, 0, 480, 222});
    canvas_clear(&canvas, 0);
    CHECK(theme_runtime_draw(&runtime, &canvas, request));
    for (int y = 30; y < 222; y++) {
        for (int x = 0; x < 480; x++) {
            CHECK_EQUAL(100 + (y % 2) * 3 + x % 3, full[y * 480 + x]);
        }
    }
    for (int top = 0; top < 222; top += 17) {
        int height = top + 17 <= 222 ? 17 : 222 - top;
        Canvas part = canvas_make(strip, (Rect){0, top, 480, height});
        canvas_clear(&part, 0);
        CHECK(theme_runtime_draw(&runtime, &part, request));
        CHECK(memcmp(full + top * 480, strip, (size_t)height * 480 * sizeof(Color)) == 0);
    }
    canvas_clear(&canvas, 0);
    canvas.clip = (Rect){12, 41, 111, 9};
    CHECK(theme_runtime_draw(&runtime, &canvas, request));
    CHECK_EQUAL(0, full[40 * 480 + 12]);
    CHECK_EQUAL(0, full[41 * 480 + 11]);
    CHECK_EQUAL(103, full[41 * 480 + 12]);
    CHECK_EQUAL(0, full[50 * 480 + 12]);
}

static void full_images_stream_in_both_orientations_and_missing_art_uses_the_base(void)
{
    static uint8_t image[528 + 480 * 222 * 2];
    static Color full[480 * 222];
    static Color strip[480 * 17];
    for (int portrait = 0; portrait < 2; portrait++) {
        reset();
        int width = portrait ? 222 : 480;
        int height = portrait ? 480 : 222;
        memcpy(image, first, 528);
        store_put32(image + 8, sizeof image);
        store_put16(image + 512, THEME_ART_HOME);
        image[514] = (uint8_t)portrait;
        store_put16(image + 516, (uint16_t)width);
        store_put16(image + 518, (uint16_t)height);
        store_put32(image + 524, (uint32_t)width * (uint32_t)height * 2);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                store_put16(image + 528 + (y * width + x) * 2, (uint16_t)(x ^ y));
            }
        }
        uint32_t crc = theme_pack_crc(UINT32_MAX, image, 12);
        store_put32(image + 12, ~theme_pack_crc(crc, image + 16, sizeof image - 16));
        static const char path[] = "/" FIRMWARE_DATA_DIRECTORY "/themes/full.gtheme";
        fake_hal_add_file(path, image, sizeof image);
        ThemeRuntime runtime = {0};
        CHECK(theme_runtime_load(&runtime, "full.gtheme"));
        ThemeArtRequest request = {.area = {0, 0, width, height},
                                   .slot = THEME_ART_HOME,
                                   .orientation = (uint8_t)portrait};
        Canvas offscreen = canvas_make(strip, (Rect){0, 0, width, 17});
        canvas_clear(&offscreen, 1234);
        ThemeArtRequest below = request;
        below.area.y = 50;
        below.area.height -= 50;
        CHECK(theme_runtime_draw(&runtime, &offscreen, below));
        CHECK(runtime.active);
        CHECK_EQUAL(1234, strip[0]);
        Canvas canvas = canvas_make(full, request.area);
        CHECK(theme_runtime_draw(&runtime, &canvas, request));
        for (int top = 0; top < height; top += 17) {
            int rows = top + 17 <= height ? 17 : height - top;
            Canvas part = canvas_make(strip, (Rect){0, top, width, rows});
            CHECK(theme_runtime_draw(&runtime, &part, request));
            CHECK(memcmp(full + top * width, strip, (size_t)rows * (size_t)width * sizeof(Color)) ==
                  0);
        }
        canvas.clip = (Rect){47, 64, 51, 19};
        canvas_clear(&canvas, UINT16_MAX);
        CHECK(theme_runtime_draw(&runtime, &canvas, request));
        CHECK_EQUAL(47 ^ 64, full[64 * width + 47]);
        request.slot = THEME_ART_NOW_PLAYING;
        CHECK(!theme_runtime_draw(&runtime, &canvas, request));
        theme_runtime_close(&runtime);
    }
}

static void damaged_packs_stay_listed_and_duplicate_ids_choose_the_newest_version(void)
{
    reset();
    second[452] ^= 1;
    ThemeCatalog catalog;
    theme_catalog_scan(&catalog);
    CHECK_EQUAL(2, catalog.count);
    CHECK(catalog.entries[0].valid);
    CHECK(!catalog.entries[1].valid);
    sample(second, 1);
    memcpy(second + 100, "2.0.0", 6);
    uint32_t crc = theme_pack_crc(UINT32_MAX, second, 12);
    store_put32(second + 12, ~theme_pack_crc(crc, second + 16, sizeof second - 16));
    theme_catalog_scan(&catalog);
    CHECK_EQUAL(1, catalog.count);
    CHECK(strcmp("second.gtheme", catalog.entries[0].filename) == 0);
}

static void every_torn_selection_write_keeps_a_whole_old_or_new_choice(void)
{
    reset();
    ThemeRuntime runtime = {0};
    theme_runtime_boot(&runtime);
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    CHECK(theme_runtime_save(&runtime, -1));
    uint32_t size;
    uint8_t before[1024];
    memcpy(before, fake_hal_internal_file_bytes(THEME_SELECTION_PATH, &size), sizeof before);
    CHECK(theme_runtime_load(&runtime, "second.gtheme"));
    CHECK(theme_runtime_save(&runtime, -1));
    uint8_t after[1024];
    memcpy(after, fake_hal_internal_file_bytes(THEME_SELECTION_PATH, &size), sizeof after);
    for (unsigned cut = 0; cut <= 512; cut++) {
        uint8_t torn[1024];
        memcpy(torn, before, sizeof torn);
        memcpy(torn + 512, after + 512, cut);
        CHECK(hal_internal_file_write(THEME_SELECTION_PATH, torn, sizeof torn));
        theme_runtime_boot(&runtime);
        CHECK(runtime.active);
        CHECK(runtime.pack.id[0] == 1 || runtime.pack.id[0] == 2);
    }
}

static void cancelling_a_preview_restores_the_saved_pack_and_builtin_choices_clear_it(void)
{
    reset();
    ThemeRuntime runtime = {0};
    theme_runtime_boot(&runtime);
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    CHECK(theme_runtime_save(&runtime, -1));
    runtime.previewing = true;
    CHECK(theme_runtime_load(&runtime, "second.gtheme"));
    theme_runtime_restore(&runtime);
    CHECK_EQUAL(1, runtime.pack.id[0]);
    CHECK(!runtime.previewing);
    theme_runtime_close(&runtime);
    CHECK(theme_runtime_save(&runtime, APPEARANCE_CLASSIC));
    theme_runtime_boot(&runtime);
    CHECK(!runtime.active);
    CHECK_EQUAL(APPEARANCE_CLASSIC, runtime.shown_base);
}

static void another_listening_work_owner_cannot_invalidate_active_art_or_the_catalogue(void)
{
    reset();
    ThemeRuntime runtime = {0};
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    ThemeCatalog *catalog = theme_picker_catalog();
    CHECK_EQUAL(2, catalog->count);
    listening_module_work.owner = &runtime;
    memset(&listening_module_work.as, 255, sizeof listening_module_work.as);
    catalog = theme_picker_catalog();
    CHECK_EQUAL(2, catalog->count);
    CHECK(catalog->entries[0].valid);
    CHECK(runtime.active);
    Color pixels[6];
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 3, 2});
    CHECK(theme_runtime_draw(&runtime, &canvas,
                             (ThemeArtRequest){.area = {0, 0, 3, 2}, .slot = THEME_ART_LIST}));
    CHECK_EQUAL(100, pixels[0]);
    CHECK_EQUAL(105, pixels[5]);
}

static void a_failed_load_preserves_the_previous_validated_pack(void)
{
    reset();
    ThemeRuntime runtime = {0};
    CHECK(theme_runtime_load(&runtime, "first.gtheme"));
    second[452] ^= 1;
    CHECK(!theme_runtime_load(&runtime, "second.gtheme"));
    CHECK(runtime.active);
    CHECK_EQUAL(1, runtime.pack.id[0]);
}

static void the_picker_highlights_the_current_builtin_then_play_saves_the_previewed_pack(void)
{
    reset();
    ThemeRuntime runtime = {0};
    Theme theme = ECHO_THEME;
    SettingsStore settings = {.settings = settings_defaults()};
    settings.settings.appearance = APPEARANCE_SUNSET;
    Toast toast = {0};
    static AppScreens screens;
    screens = (AppScreens){.context = {.theme = &theme,
                                       .styled_theme = &theme,
                                       .themes = &runtime,
                                       .settings = &settings,
                                       .toast = &toast}};
    ThemePicker *picker = &screens.modules.listening.themes;
    theme_picker_init(picker, &screens);
    picker->screen.enter(picker, 0);
    CHECK_EQUAL(APPEARANCE_SUNSET, picker->list.selected);
    for (int at = 0; at < 3; at++) {
        picker->screen.handle_key(
            picker, (KeyEvent){.key = KEY_VOLUME_UP, .gesture = KEY_GESTURE_PRESS}, 100);
    }
    CHECK_EQUAL(APPEARANCE_THEME_COUNT, picker->list.selected);
    const char *credit = picker->model.detail(picker, picker->list.selected);
    CHECK(strstr(credit, "Maker") != NULL);
    CHECK(strstr(credit, "Low contrast") != NULL);
    CHECK(runtime.active);
    CHECK(runtime.previewing);
    ScreenAction chosen = picker->screen.handle_key(
        picker, (KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_PRESS}, 200);
    CHECK_EQUAL(SCREEN_ACTION_POP, chosen.type);
    CHECK_EQUAL(1, runtime.saved_id[0]);
    CHECK(!runtime.previewing);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_saved_pack_survives_restart_and_a_damaged_or_missing_pack_falls_back),
        TEST_CASE(applying_a_pack_preserves_fonts_layout_and_all_palette_roles),
        TEST_CASE(pack_art_draws_only_through_the_loaded_ui_module),
        TEST_CASE(strips_and_clipped_rectangles_match_a_full_render),
        TEST_CASE(full_images_stream_in_both_orientations_and_missing_art_uses_the_base),
        TEST_CASE(damaged_packs_stay_listed_and_duplicate_ids_choose_the_newest_version),
        TEST_CASE(every_torn_selection_write_keeps_a_whole_old_or_new_choice),
        TEST_CASE(cancelling_a_preview_restores_the_saved_pack_and_builtin_choices_clear_it),
        TEST_CASE(another_listening_work_owner_cannot_invalidate_active_art_or_the_catalogue),
        TEST_CASE(a_failed_load_preserves_the_previous_validated_pack),
        TEST_CASE(the_picker_highlights_the_current_builtin_then_play_saves_the_previewed_pack),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
