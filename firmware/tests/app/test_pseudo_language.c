#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "settings/settings.h"
#include "settings_store.h"
#include "test_pack.h"
#include "text_spy.h"

enum {
    START_MS = 1000,
    KEY_HOLD_MS = 50,
    FRAME_MS = 120,
    SETTLE_FRAMES = 4,
    HOME_ROWS = 6,
    HOME_SETTINGS_ROW = 1,
    HOME_NOW_PLAYING_ROW = 2,
    HOME_UP_NEXT_ROW = 3,
    MUSIC_ENTRIES = 11,
    MUSIC_ALBUMS_ROW = 4,
    MUSIC_ALL_SONGS_ROW = 7,
    SETTINGS_ROWS = 23,
    SETTINGS_BLUETOOTH_ROW = 3,
    SETTINGS_SLEEP_ROW = 4,
    SETTINGS_OUTPUT_ROW = 14,
    SETTINGS_EQUALIZER_ROW = 15,
    SETTINGS_CUSTOM_EQ_ROW = 16,
    SETTINGS_RESCAN_ROW = 20,
    SETTINGS_USB_MODE_ROW = 21,
    ABOUT_ROWS = 14,
    EQUALIZER_SAVE_ROW = 10,
    EQUALIZER_SLOT_OPTION = 8,
    MINIMUM_PACK_TEXTS = 400,
    WORD_CAPACITY = 64
};

static const char *const KNOWN_WORDS[] = {
    "kHz", "kbps", "mV", "MHz", "dB", "Hz", "ms", "FLAC", "MP3", "WAV", "AAC", "ALAC",
};

static const char *const ABOUT_WORDS[] = {
    "high", "low", "boot", "fn",  "in",  "out",  "pull", "none", "play",
    "vol",  "no",  "mic",  "min", "dip", "dips", "Auto", "Kept", "off",
};

static const char *const KNOWN_TEXTS[] = {
    FIRMWARE_NAME, FIRMWARE_SHORT_NAME, "English", "test", "OnePlus Buds 3", "Kitchen",
};

static const char STAR[] = "\xE2\x98\x85 ";

static uint8_t pseudo[TEST_PACK_CAPACITY];
static uint8_t english[TEST_PACK_CAPACITY];
static uint32_t pseudo_size;
static uint32_t english_size;
static uint32_t now_ms;
static const char *variant = "";
static int texts_from_the_pack;
static bool portrait;
static bool engineering_values;
static TextSize variant_size;
static Orientation variant_orientation;

static void add_player_files(bool with_library)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    if (with_library) {
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", bytes, size);
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/build.tmp", NULL, 16 * 1024 * 1024);
    }
    for (size_t index = 0; index < card_files_count(); index++) {
        HalFile file;
        if (card_files_slow(index) && !hal_file_open(&file, card_files_path(index))) {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
}

static void start(TextSize size, Orientation orientation, bool with_library)
{
    static uint8_t saved[SETTINGS_FILE_SIZE];
    Settings settings = settings_defaults();
    settings.text_size = size;
    settings.orientation = orientation;
    settings_encode(&settings, saved);
    fake_hal_reset();
    i18n_detach();
    fake_hal_add_language_pack(0, pseudo, pseudo_size);
    fake_hal_add_language_pack(0, english, english_size);
    fake_hal_add_file(SETTINGS_STORE_PATH, saved, sizeof saved);
    add_player_files(with_library);
    now_ms = START_MS;
    portrait = orientation != ORIENTATION_LANDSCAPE;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    text_spy_reset();
}

static void frames(int count)
{
    for (int frame = 0; frame < count; frame++) {
        now_ms += FRAME_MS;
        fake_hal_set_milliseconds(now_ms);
        app_step();
    }
}

static void tap(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + KEY_HOLD_MS);
    now_ms += KEY_HOLD_MS + 10;
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void hold(Key key, uint32_t duration_ms)
{
    fake_hal_queue_transition(key, true, now_ms);
    now_ms += duration_ms;
    fake_hal_set_milliseconds(now_ms);
    app_step();
    fake_hal_queue_transition(key, false, now_ms);
    frames(1);
}

static void down(int times)
{
    for (int count = 0; count < times; count++) {
        tap(portrait ? KEY_VOLUME_DOWN : KEY_VOLUME_UP);
    }
}

static void back(int times)
{
    for (int count = 0; count < times; count++) {
        tap(KEY_BACK);
        frames(SETTLE_FRAMES);
    }
}

static bool is_ascii_letter(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

static int bracket_balance(const char *text)
{
    int balance = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        balance += *cursor == '[' ? 1 : *cursor == ']' ? -1 : 0;
    }
    return balance;
}

static bool is_listed(const char *word, const char *const *words, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (strcmp(word, words[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool starts_a_listed_word(const char *word, const char *const *words, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (strncmp(word, words[index], strlen(word)) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_known_word(const char *word)
{
    return is_listed(word, KNOWN_WORDS, sizeof KNOWN_WORDS / sizeof KNOWN_WORDS[0]) ||
           (engineering_values &&
            starts_a_listed_word(word, ABOUT_WORDS, sizeof ABOUT_WORDS / sizeof ABOUT_WORDS[0]));
}

static bool is_data(const char *text)
{
    uint32_t size = 0;
    text += strncmp(text, STAR, sizeof STAR - 1) == 0 ? sizeof STAR - 1 : 0;
    const uint8_t *library = fixture_bytes(&size);
    size_t length = strlen(text);
    for (size_t index = 0; index < sizeof KNOWN_TEXTS / sizeof KNOWN_TEXTS[0]; index++) {
        if (strcmp(text, KNOWN_TEXTS[index]) == 0) {
            return true;
        }
    }
    for (uint32_t offset = 0; length > 0 && offset + length <= size; offset++) {
        if (memcmp(library + offset, text, length) == 0) {
            return true;
        }
    }
    return false;
}

static bool has_english_outside_brackets(const TextSpyEntry *entry)
{
    int depth = bracket_balance(entry->rest) < 0 ? -bracket_balance(entry->rest) : 0;
    char word[WORD_CAPACITY];
    size_t length = 0;
    for (const char *cursor = entry->shown;; cursor++) {
        if (is_ascii_letter(*cursor) && depth == 0 && length < sizeof word - 1) {
            word[length++] = *cursor;
            continue;
        }
        word[length] = '\0';
        if (length > 1 && !is_known_word(word)) {
            return true;
        }
        length = 0;
        depth += *cursor == '[' ? 1 : *cursor == ']' && depth > 0 ? -1 : 0;
        if (*cursor == '\0') {
            return false;
        }
    }
}

static void check_entry(const char *screen, const TextSpyEntry *entry)
{
    bool literal =
        has_english_outside_brackets(entry) && !is_data(entry->rest) && !is_data(entry->shown);
    bool cut_in_a_buffer = bracket_balance(entry->rest) > 0;
    if (literal) {
        printf("  %s, %s: \"%s\" is not from the language pack\n", variant, screen, entry->shown);
    }
    if (cut_in_a_buffer) {
        printf("  %s, %s: \"%s\" lost its end before it was drawn\n", variant, screen, entry->rest);
    }
    CHECK(!literal);
    CHECK(!cut_in_a_buffer);
    texts_from_the_pack += strchr(entry->shown, '[') != NULL ? 1 : 0;
}

static int show(const char *screen)
{
    frames(SETTLE_FRAMES);
    int before = texts_from_the_pack;
    for (int index = 0; index < text_spy_count(); index++) {
        check_entry(screen, text_spy_entry(index));
    }
    CHECK(text_spy_count() > 0);
    CHECK(text_spy_count() < TEXT_SPY_CAPACITY);
    text_spy_reset();
    return texts_from_the_pack - before;
}

static void show_titled(const char *screen, StringId title)
{
    bool there = strcmp(i18n_text(title), app_screen_title()) == 0;
    if (!there) {
        printf("  %s: expected %s, on \"%s\"\n", variant, screen, app_screen_title());
    }
    CHECK(there);
    CHECK(show(screen) > 0);
}

static void show_toast(const char *screen, StringId text)
{
    const char *toast = app_toast_text();
    bool shown = toast != NULL && (text == STRING_COUNT || strcmp(i18n_text(text), toast) == 0);
    if (!shown) {
        printf("  %s, %s: the toast is \"%s\"\n", variant, screen, toast != NULL ? toast : "");
    }
    CHECK(shown);
    CHECK(toast != NULL && toast[0] == '[');
    CHECK(show(screen) > 0);
}

static void begin(void)
{
    start(variant_size, variant_orientation, true);
    frames(SETTLE_FRAMES);
    text_spy_reset();
}

static void open_music_row(int row)
{
    begin();
    tap(KEY_PLAY);
    down(row);
}

static void open_settings_row(int row)
{
    begin();
    down(HOME_SETTINGS_ROW);
    tap(KEY_PLAY);
    down(row);
}

static void walk_music(void)
{
    static const StringId TITLES[MUSIC_ENTRIES] = {
        STR_SEARCH,  STR_COVER_FLOW, STR_ARTISTS,   STR_ALBUM_ARTISTS, STR_ALBUMS,       STR_GENRES,
        STR_FOLDERS, STR_ALL_SONGS,  STR_PLAYLISTS, STR_FAVORITES,     STR_PERSONAL_TAGS};
    open_music_row(0);
    show_titled("Music", STR_MUSIC);
    for (int entry = 1; entry < MUSIC_ENTRIES; entry++) {
        open_music_row(entry);
        tap(KEY_PLAY);
        show_titled("a Music list", TITLES[entry]);
    }
}

static void walk_cover_flow(void)
{
    open_music_row(1);
    tap(KEY_PLAY);
    CHECK(strcmp(i18n_text(STR_COVER_FLOW), app_screen_title()) == 0);
    show("Cover Flow");
    tap(portrait ? KEY_VOLUME_UP : KEY_VOLUME_DOWN);
    CHECK(show("Cover Flow on the random album") >= 2);
    down(1);
    hold(KEY_PLAY, 700);
    show_toast("Cover Flow after a favorite", STR_FAVORITE_ADDED);
    hold(KEY_PLAY, 700);
    show_toast("Cover Flow after a favorite is removed", STR_FAVORITE_REMOVED);
}

static void walk_song_actions(void)
{
    static const StringId TOASTS[3] = {STR_PLAYING_NEXT, STR_ADDED_TO_UP_NEXT, STRING_COUNT};
    for (int action = 0; action < 3; action++) {
        open_music_row(MUSIC_ALL_SONGS_ROW);
        tap(KEY_PLAY);
        tap(KEY_PLAY);
        back(1);
        down(1);
        hold(KEY_PLAY, 700);
        CHECK_EQUAL(4, app_screen_depth());
        CHECK(show("the song actions") >= 3);
        down(action);
        tap(KEY_PLAY);
        show_toast("a song action", TOASTS[action]);
    }
}

static void walk_now_playing(void)
{
    open_music_row(MUSIC_ALL_SONGS_ROW);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK(strcmp(i18n_text(STR_NOW_PLAYING), app_screen_title()) == 0);
    show("Now Playing");
    tap(portrait ? KEY_VOLUME_DOWN : KEY_VOLUME_UP);
    CHECK(show("Now Playing with the volume") > 0);
    frames(20);
    hold(KEY_PLAY, 700);
    CHECK(show("Now Playing while seeking") > 0);
    tap(KEY_PLAY);
    back(3);
    down(HOME_UP_NEXT_ROW);
    tap(KEY_PLAY);
    show_titled("Up Next", STR_UP_NEXT);
}

static void walk_settings_rows(void)
{
    open_settings_row(0);
    show_titled("Settings", STR_SETTINGS);
    for (int row = 1; row < SETTINGS_ROWS; row++) {
        down(1);
        show_titled("a Settings row", STR_SETTINGS);
    }
}

static void walk_settings_choices(void)
{
    static const StringId TITLES[SETTINGS_ROWS] = {
        STR_ABOUT,       STR_SHUFFLE,     STR_REPEAT,           STR_BLUETOOTH,
        STR_SLEEP_TIMER, STR_AUTO_OFF,    STR_SCREEN_TIMEOUT,   STR_BRIGHTNESS,
        STR_COLOR,       STR_TEXT_SIZE,   STR_ORIENTATION,      STR_LANGUAGE,
        STR_SKIP_SONGS,  STR_GAIN,        STR_OUTPUT,           STR_EQUALIZER,
        STR_CUSTOM_EQ,   STR_REPLAY_GAIN, STR_PREVENT_CLIPPING, STR_START_SCREEN,
        STR_SETTINGS,    STR_USB_MODE,    STR_USB_ANSWER};
    for (int row = 1; row < SETTINGS_ROWS; row++) {
        if (row == SETTINGS_RESCAN_ROW) {
            continue;
        }
        open_settings_row(row);
        tap(KEY_PLAY);
        show_titled("a screen opened from Settings", TITLES[row]);
        for (int option = 0; option < 4; option++) {
            down(1);
            show("its other options");
        }
    }
}

static void choose_option(int row, int option)
{
    open_settings_row(row);
    tap(KEY_PLAY);
    down(option);
    frames(SETTLE_FRAMES);
    text_spy_reset();
    tap(KEY_PLAY);
}

static void walk_settings_toasts(void)
{
    choose_option(SETTINGS_SLEEP_ROW, 1);
    show_toast("the sleep timer toast", STRING_COUNT);
    choose_option(SETTINGS_SLEEP_ROW, 0);
    show_toast("the sleep timer off toast", STR_SLEEP_OFF);
    choose_option(SETTINGS_OUTPUT_ROW, 0);
    show_toast("the output toast", STR_OUTPUT_FOLLOWS);
    choose_option(SETTINGS_OUTPUT_ROW, 1);
    show_toast("the 3.5 mm output toast", STR_OUTPUT_USING_35);
    choose_option(SETTINGS_USB_MODE_ROW, 2);
    show_toast("the USB mode toast", STR_USB_PLAYS_SOUND);
    open_settings_row(SETTINGS_RESCAN_ROW);
    text_spy_reset();
    tap(KEY_PLAY);
    frames(2);
    show_toast("the rescan result", STR_SCAN_NO_MUSIC);
}

static void walk_equalizer(void)
{
    open_settings_row(SETTINGS_CUSTOM_EQ_ROW);
    tap(KEY_PLAY);
    show_titled("Custom EQ", STR_CUSTOM_EQ);
    tap(KEY_PLAY);
    CHECK(show("Custom EQ while adjusting a band") > 0);
    tap(KEY_PLAY);
    down(EQUALIZER_SAVE_ROW);
    tap(KEY_PLAY);
    CHECK(show("Custom EQ while choosing a slot") > 0);
    tap(KEY_PLAY);
    show_toast("Custom EQ after saving a slot", STRING_COUNT);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    show_titled("Custom EQ with a used slot", STR_CUSTOM_EQ);
    back(1);
    tap(portrait ? KEY_VOLUME_UP : KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    down(EQUALIZER_SLOT_OPTION);
    show_titled("the equalizer choices with a saved slot", STR_EQUALIZER);
}

static void find_devices(void)
{
    HalBluetoothStatus *status = fake_hal_bluetooth();
    status->state = HAL_BLUETOOTH_SCANNING;
    status->device_count = 2;
    strcpy(status->devices[0].name, "OnePlus Buds 3");
    status->devices[0].address[0] = 0x5C;
    strcpy(status->devices[1].name, "Kitchen");
    status->devices[1].address[0] = 0x22;
}

static void walk_bluetooth(void)
{
    open_settings_row(SETTINGS_BLUETOOTH_ROW);
    tap(KEY_PLAY);
    show_titled("Bluetooth off", STR_BLUETOOTH);
    tap(KEY_PLAY);
    frames(7);
    show_titled("Bluetooth on", STR_BLUETOOTH);
    down(1);
    tap(KEY_PLAY);
    find_devices();
    show_titled("Bluetooth searching", STR_BLUETOOTH);
    for (int row = 0; row < 2; row++) {
        down(1);
        show_titled("a Bluetooth device found", STR_BLUETOOTH);
    }
    tap(KEY_PLAY);
    show_titled("Bluetooth pairing", STR_BLUETOOTH);
    back(2);
    CHECK(show("Home with Bluetooth on") >= 2);
}

static void walk_about(void)
{
    open_settings_row(0);
    tap(KEY_PLAY);
    engineering_values = true;
    show_titled("About", STR_ABOUT);
    for (int row = 1; row < ABOUT_ROWS; row++) {
        down(1);
        show_titled("an About row", STR_ABOUT);
    }
    engineering_values = false;
}

static void walk_power(void)
{
    begin();
    fake_hal_set_battery((HalBattery){
        .known = true, .percent = 64, .millivolts = 3900, .usb_power = true, .charging = true});
    hold(KEY_POWER, 2000);
    frames(4);
    CHECK(show("Goodbye") > 0);
    frames(14);
    CHECK(app_charging());
    CHECK(show("the charge screen while charging") >= 2);
    fake_hal_set_battery(
        (HalBattery){.known = true, .percent = 100, .millivolts = 4200, .usb_power = true});
    tap(KEY_PLAY);
    frames(7);
    CHECK(show("the charge screen when charged") >= 2);
}

static void walk_usb_prompt(void)
{
    begin();
    app_ask_usb_mode();
    show_titled("the USB prompt", STR_USB_CONNECTED);
    begin();
    app_open_usb_modes();
    show_titled("USB mode opened by hand", STR_USB_MODE);
}

static void walk_listening(void)
{
    begin();
    tap(portrait ? KEY_VOLUME_UP : KEY_VOLUME_DOWN);
    tap(portrait ? KEY_VOLUME_UP : KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    show_titled("Listening", STR_LISTENING);
    tap(portrait ? KEY_VOLUME_UP : KEY_VOLUME_DOWN);
    tap(KEY_PLAY);
    CHECK(strcmp(i18n_text(STR_EXPORT_PLAYS), app_screen_title()) == 0);
    CHECK(show("Export plays") >= 3);
}

static void walk_everything(void)
{
    begin();
    tap(portrait ? KEY_VOLUME_DOWN : KEY_VOLUME_UP);
    CHECK(strcmp(FIRMWARE_SHORT_NAME, app_screen_title()) == 0);
    CHECK(show("Home") >= 2);
    walk_music();
    walk_cover_flow();
    walk_settings_rows();
    walk_settings_choices();
    walk_settings_toasts();
    walk_equalizer();
    walk_bluetooth();
    walk_about();
    walk_song_actions();
    walk_now_playing();
    walk_listening();
    walk_usb_prompt();
    walk_power();
}

static void walk_without_a_library(void)
{
    start(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE, false);
    frames(SETTLE_FRAMES);
    text_spy_reset();
    tap(KEY_PLAY);
    down(MUSIC_ALBUMS_ROW);
    tap(KEY_PLAY);
    show_titled("No library", STR_NO_LIBRARY);
    back(2);
    CHECK(show("Home without a library") >= 2);
}

static void every_screen_draws_only_text_from_the_pack_at(TextSize size, Orientation orientation,
                                                          const char *name)
{
    variant = name;
    variant_size = size;
    variant_orientation = orientation;
    texts_from_the_pack = 0;
    walk_everything();
    CHECK(texts_from_the_pack > MINIMUM_PACK_TEXTS);
}

static void medium_landscape_screens_draw_only_text_from_the_pack(void)
{
    every_screen_draws_only_text_from_the_pack_at(TEXT_SIZE_MEDIUM, ORIENTATION_LANDSCAPE,
                                                  "medium landscape");
}

static void small_landscape_screens_draw_only_text_from_the_pack(void)
{
    every_screen_draws_only_text_from_the_pack_at(TEXT_SIZE_SMALL, ORIENTATION_LANDSCAPE,
                                                  "small landscape");
}

static void large_landscape_screens_draw_only_text_from_the_pack(void)
{
    every_screen_draws_only_text_from_the_pack_at(TEXT_SIZE_LARGE, ORIENTATION_LANDSCAPE,
                                                  "large landscape");
}

static void medium_portrait_screens_draw_only_text_from_the_pack(void)
{
    every_screen_draws_only_text_from_the_pack_at(TEXT_SIZE_MEDIUM, ORIENTATION_PORTRAIT,
                                                  "medium portrait");
}

static void small_portrait_screens_draw_only_text_from_the_pack(void)
{
    every_screen_draws_only_text_from_the_pack_at(TEXT_SIZE_SMALL, ORIENTATION_PORTRAIT,
                                                  "small portrait");
}

static void large_portrait_screens_draw_only_text_from_the_pack(void)
{
    every_screen_draws_only_text_from_the_pack_at(TEXT_SIZE_LARGE, ORIENTATION_PORTRAIT,
                                                  "large portrait");
}

static void the_screens_without_a_library_draw_only_text_from_the_pack(void)
{
    variant = "no library";
    walk_without_a_library();
}

static void an_english_literal_would_be_caught(void)
{
    TextSpyEntry literal = {.shown = "Settings", .rest = "Settings"};
    TextSpyEntry from_pack = {.shown = "[S\xC3\xA9tt\xC3\xADngs!!!]",
                              .rest = "[S\xC3\xA9tt\xC3\xADngs!!!]"};
    TextSpyEntry cut_by_a_label = {.shown = "[S\xC3\xA9tt", .rest = "[S\xC3\xA9tt\xC3\xADngs!!!]"};
    TextSpyEntry second_line = {.shown = "t\xC3\xB3 \xC3\xA1"
                                         "dd \xC3\xADt!!!]",
                                .rest = "t\xC3\xB3 \xC3\xA1"
                                        "dd \xC3\xADt!!!]"};
    TextSpyEntry cut_in_a_buffer = {.shown = "[Ch\xC3\xA1pt\xC3\xA9r 1 \xC3\xB3"
                                             "f",
                                    .rest = "[Ch\xC3\xA1pt\xC3\xA9r 1 \xC3\xB3"
                                            "f"};
    TextSpyEntry mixed = {.shown = "[16-b\xC3\xADt!] \xC2\xB7 8 kHz",
                          .rest = "[16-b\xC3\xADt!] \xC2\xB7 8 kHz"};
    TextSpyEntry unit_and_word = {.shown = "8 kHz stereo", .rest = "8 kHz stereo"};
    CHECK(has_english_outside_brackets(&literal));
    CHECK(!has_english_outside_brackets(&from_pack));
    CHECK(!has_english_outside_brackets(&cut_by_a_label));
    CHECK(!has_english_outside_brackets(&second_line));
    CHECK(!has_english_outside_brackets(&mixed));
    CHECK(has_english_outside_brackets(&unit_and_word));
    CHECK(bracket_balance(cut_in_a_buffer.rest) > 0);
    CHECK(bracket_balance(cut_by_a_label.rest) == 0);
    CHECK(bracket_balance(second_line.rest) < 0);
}

int main(void)
{
    english_size = test_pack_build(english, (TestPackLanguage){"en", "English", NULL});
    pseudo_size = test_pack_load(ECHO_PSEUDO_PACK, pseudo, sizeof pseudo);
    static const TestCase cases[] = {
        TEST_CASE(an_english_literal_would_be_caught),
        TEST_CASE(medium_landscape_screens_draw_only_text_from_the_pack),
        TEST_CASE(small_landscape_screens_draw_only_text_from_the_pack),
        TEST_CASE(large_landscape_screens_draw_only_text_from_the_pack),
        TEST_CASE(medium_portrait_screens_draw_only_text_from_the_pack),
        TEST_CASE(small_portrait_screens_draw_only_text_from_the_pack),
        TEST_CASE(large_portrait_screens_draw_only_text_from_the_pack),
        TEST_CASE(the_screens_without_a_library_draw_only_text_from_the_pack),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
