#include <string.h>

#include "app_screens.h"
#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "gfx/text.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/toast.h"

enum {
    TEXT_CAPACITY = 128,
    HINT_LINES = 3
};

static const StringId NOTICE_TITLES[] = {
    STR_CARD_FORMAT,
    STR_CARD_FORMAT_CONFIRM,
    STR_USB,
    STR_USB_DAC_TITLE,
    STR_USB_UNPLUGGED,
    STR_UPDATE,
    STR_UPDATE_INSTALLING,
    STR_UPDATE_INSTALLING_STOCK,
    STR_UPDATE_FAILED,
    STR_UPDATE_INSTALLED,
    STR_UPDATE_INSTALLED_STOCK,
    STR_UPDATE_NOT_INSTALLED,
    STR_SCANNING,
    STR_FINDING_COVERS,
    STR_PREPARING_STORAGE,
    STR_MUSIC,
};

static const StringId NOTICE_MESSAGES[] = {
    STR_USB_STARTING,        STR_USB_EJECT_DRIVE,     STR_USB_EJECT_DRIVES,
    STR_USB_UNPLUGGED_HINT,  STR_USB_CODE_MISSING,    STR_RESTART_TO_FINISH,
    STR_USB_DAC_FAILED,      STR_UPDATE_FAILED_HINT,  STR_UPDATE_UNPLUG,
    STR_UPDATE_UNPLUG_FIRST, STR_UPDATE_TURNING_OFF,  STR_CHECKING_MUSIC,
    STR_SCAN_STARTING,       STR_CARD_FORMAT_INSERT,  STR_CARD_FORMAT_PLAYING,
    STR_CARD_FORMAT_USB,     STR_CARD_FORMAT_BATTERY, STR_CARD_FORMAT_CHANGED,
    STR_CARD_FORMAT_DONE,    STR_CARD_FORMAT_FAILED,
};

typedef struct Pattern {
    StringId id;
    const char *arguments[3];
} Pattern;

static const Pattern NOTICE_PATTERNS[] = {
    {STR_CARD_FORMAT_PROGRESS, {"100"}},
    {STR_UPDATE_PROGRESS, {"100"}},
    {STR_UPDATE_CHECKING, {FIRMWARE_UPDATE_FILE}},
    {STR_UPDATE_REFUSED, {FIRMWARE_UPDATE_FILE}},
    {STR_SCAN_TRACKS, {"65535"}},
    {STR_COVERS_DONE, {"2673", "2673"}},
    {STR_PREPARING_FILE, {"13", "13"}},
};

static const StringId SCREEN_TITLES[] = {
    STR_CLEAR_HISTORY,
    STR_MUSIC,
    STR_AUDIOBOOKS,
    STR_PODCASTS,
    STR_SETTINGS,
    STR_NOW_PLAYING,
    STR_UP_NEXT,
    STR_FAVORITES,
    STR_NO_LIBRARY,
    STR_SHUFFLE,
    STR_REPEAT,
    STR_BLUETOOTH,
    STR_SLEEP_TIMER,
    STR_AUTO_OFF,
    STR_BRIGHTNESS,
    STR_COLOR,
    STR_SCREEN_TIMEOUT,
    STR_CLOCK_FORMAT,
    STR_SET_CLOCK,
    STR_TEXT_SIZE,
    STR_ORIENTATION,
    STR_LANGUAGE,
    STR_SKIP_SONGS,
    STR_GAIN,
    STR_OUTPUT,
    STR_EQUALIZER,
    STR_CUSTOM_EQ,
    STR_START_SCREEN,
    STR_USB_MODE,
    STR_ABOUT,
    STR_THEME,
    STR_CONTROLS,
    STR_MIXTAPES,
    STR_MIXTAPE_NAME,
    STR_LISTENING,
    STR_TOP_ARTISTS,
    STR_TOP_ALBUMS,
    STR_TOP_SONGS,
    STR_LISTENING_TOTALS,
    STR_LYRICS,
    STR_REPLAY_GAIN,
    STR_PREVENT_CLIPPING,
    STR_CLOCK,
    STR_CALENDAR,
    STR_BATTERY_PERCENTAGE,
    STR_SMART_LISTS,
    STR_RECENTLY_ADDED,
    STR_RECENTLY_PLAYED,
    STR_MOST_PLAYED,
    STR_NEVER_PLAYED,
    STR_FORGOTTEN_FAVORITES,
    STR_HOME_SCREEN,
    STR_HOME_MOVING,
    STR_REWIND_ON_RESUME,
    STR_SAFE_START_VOLUME,
    STR_TRANSITIONS,
    STR_TRANSITION_LENGTH,
    STR_USB_CONNECTED,
    STR_USB_ANSWER,
};

static const StringId EMPTY_STATES[] = {
    STR_NOTHING_HERE,        STR_NO_LIBRARY_HINT,   STR_NO_FAVORITES_HINT,
    STR_NO_BOOKS_HINT,       STR_NO_SHOWS_HINT,     STR_NOTHING_PLAYING,
    STR_NOTHING_IS_PLAYING,  STR_SMART_NONE_ADDED,  STR_SMART_NONE_PLAYED,
    STR_SMART_NONE_LISTENED, STR_SMART_ALL_PLAYED,  STR_SMART_NONE_FORGOTTEN,
    STR_HISTORY_UNAVAILABLE, STR_TIME_UNKNOWN_HINT,
};

static const Pattern SCREEN_MESSAGE_PATTERNS[] = {
    {STR_SMART_CHECKING, {"100"}},
    {STR_SMART_FINDING, {"100"}},
};

static const StringId TOASTS[] = {
    STR_LOCKED_HINT,        STR_BUTTONS_LOCKED,
    STR_BUTTONS_UNLOCKED,   STR_STORAGE_FULL,
    STR_NO_SONGS_IN_FOLDER, STR_PLAYING_NEXT,
    STR_ADDED_TO_UP_NEXT,   STR_PLAYING,
    STR_UP_NEXT_FULL,       STR_SONG_FAILED,
    STR_FAVORITE_ADDED,     STR_FAVORITE_REMOVED,
    STR_FAVORITE_ALREADY,   STR_FAVORITE_FAILED,
    STR_FAVORITES_FULL,     STR_FAVORITES_NOT_SAVED,
    STR_TYPE_UNSUPPORTED,   STR_M4A_UNSUPPORTED,
    STR_TRACK_UNREADABLE,   STR_SLEEP_OFF,
    STR_OUTPUT_FOLLOWS,     STR_OUTPUT_USING_35,
    STR_OUTPUT_USING_44,    STR_USB_SHOWS_DRIVES,
    STR_USB_PLAYS_SOUND,    STR_SETTINGS_NOT_SAVED,
    STR_LANGUAGE_FAILED,    STR_EQ_SLOT_NOT_SAVED,
    STR_EQ_FILE_UNREADABLE, STR_SCAN_NO_MUSIC,
    STR_SCAN_NO_MEMORY,     STR_SCAN_TOO_MANY,
    STR_SCAN_NO_SCRATCH,    STR_SCAN_FOLDER_TOO_BIG,
    STR_SCAN_PATH_TOO_LONG, STR_SCAN_WRITE_FAILED,
    STR_SCAN_NO_ROOM,       STR_SCAN_NO_CODE,
    STR_RESTART_TO_FINISH,  STR_HOME_MOVE_CANCELLED,
    STR_HOME_WAS_RESET,
};

static const Pattern TOAST_PATTERNS[] = {
    {STR_SLEEP_IN, {"120 min"}},
    {STR_EQ_SAVED, {"8"}},
    {STR_RATE_UNSUPPORTED, {"352.8 kHz"}},
    {STR_LIBRARY_COVERS, {"Library: 65535 tracks", "2673"}},
    {STR_LIBRARY_NO_COVERS, {"Library: 65535 tracks"}},
    {STR_VOLUME_LOWERED, {"60"}},
};

static const Pattern EXPORT_HEADLINES[] = {
    {STR_EXPORT_WORKING, {""}},
    {STR_EXPORT_DONE, {""}},
    {STR_EXPORT_NONE, {""}},
    {STR_EXPORT_FAILED, {""}},
};

static const Pattern EXPORT_DETAILS[] = {
    {STR_EXPORT_PROGRESS, {"100"}},
    {STR_EXPORT_SAVED, {"4096", "/.scrobbler.log"}},
    {STR_EXPORT_ONE_SAVED, {"1", "/.scrobbler.log"}},
    {STR_EXPORT_UNKNOWN, {""}},
    {STR_LISTENING_HINT, {""}},
    {STR_EXPORT_WRITE_FAILED, {"/.scrobbler.log"}},
    {STR_STORAGE_FULL, {""}},
    {STR_HISTORY_UNAVAILABLE, {""}},
};

static const StringId BLUETOOTH_HINTS[] = {
    STR_BT_HINT_DISCONNECT, STR_BT_HINT_CONNECTING, STR_BT_HINT_CONNECT, STR_BT_HINT_TURN_OFF,
    STR_BT_HINT_TURN_ON,    STR_BT_HINT_SEARCHING,  STR_BT_HINT_SEARCH,  STR_BT_HINT_PAIR,
};

static const char *pattern_text(Pattern pattern)
{
    static char text[TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format(&builder, pattern.id, pattern.arguments, 3);
    return text;
}

static Theme themed(int size, int orientation)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, (Orientation)orientation);
    return theme;
}

static Theme notice_theme(int orientation)
{
    Theme theme = NOTICE_THEME;
    default_theme_set_orientation(&theme, (Orientation)orientation);
    return theme;
}

static bool wraps_whole(const Font *font, const char *text, int width, int max_lines)
{
    const char *cursor = text;
    for (int line = 0; line < max_lines; line++) {
        char shown[TEXT_CAPACITY] = {0};
        size_t bytes = label_wrap_line(font, cursor, width);
        memcpy(shown, cursor, bytes);
        if (text_width(font, shown) > width) {
            return false;
        }
        cursor += bytes;
        while (*cursor == ' ') {
            cursor++;
        }
        cursor += *cursor == '\n' ? 1 : 0;
        if (*cursor == '\0') {
            return true;
        }
    }
    return false;
}

static void every_notice_title_fits_the_header_in_both_orientations(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = notice_theme(orientation);
        int room = header_title_bounds(&theme).width;
        for (size_t index = 0; index < sizeof NOTICE_TITLES / sizeof NOTICE_TITLES[0]; index++) {
            CHECK(text_width(theme.fonts.header, i18n_text(NOTICE_TITLES[index])) <= room);
        }
    }
}

static bool notice_fits(const Theme *theme, const char *text)
{
    Rect content = theme_content_area(theme);
    int lines = content.height / font_line_height(theme->fonts.body);
    return wraps_whole(theme->fonts.body, text, content.width - 2 * theme->metrics.padding, lines);
}

static void every_notice_message_wraps_whole_into_the_notice_in_both_orientations(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = notice_theme(orientation);
        for (size_t index = 0; index < sizeof NOTICE_MESSAGES / sizeof NOTICE_MESSAGES[0];
             index++) {
            CHECK(notice_fits(&theme, i18n_text(NOTICE_MESSAGES[index])));
        }
        for (size_t index = 0; index < sizeof NOTICE_PATTERNS / sizeof NOTICE_PATTERNS[0];
             index++) {
            CHECK(notice_fits(&theme, pattern_text(NOTICE_PATTERNS[index])));
        }
    }
}

static void every_screen_title_fits_the_header_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = themed(size, orientation);
            int room = header_title_bounds(&theme).width;
            for (size_t index = 0; index < sizeof SCREEN_TITLES / sizeof SCREEN_TITLES[0];
                 index++) {
                CHECK(text_width(theme.fonts.header, i18n_text(SCREEN_TITLES[index])) <= room);
            }
        }
    }
}

static void every_empty_screen_message_wraps_whole_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = themed(size, orientation);
            for (size_t index = 0; index < sizeof EMPTY_STATES / sizeof EMPTY_STATES[0]; index++) {
                CHECK(notice_fits(&theme, i18n_text(EMPTY_STATES[index])));
            }
            for (size_t index = 0;
                 index < sizeof SCREEN_MESSAGE_PATTERNS / sizeof SCREEN_MESSAGE_PATTERNS[0];
                 index++) {
                CHECK(notice_fits(&theme, pattern_text(SCREEN_MESSAGE_PATTERNS[index])));
            }
        }
    }
}

static void every_month_title_and_clock_hint_fits_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = themed(size, orientation);
            int room = header_title_bounds(&theme).width;
            int line = theme.metrics.screen_width - 2 * theme.metrics.padding;
            for (int month = 0; month < 12; month++) {
                char title[64];
                StringBuilder builder = string_builder_make(title, sizeof title);
                const char *arguments[] = {i18n_text((StringId)(STR_JANUARY + month)), "2026"};
                i18n_format(&builder, STR_CALENDAR_MONTH, arguments, 2);
                CHECK(text_width(theme.fonts.header, title) <= room);
            }
            CHECK(text_width(theme.fonts.small, i18n_text(STR_CLOCK_HINT)) <= line);
            CHECK(text_width(theme.fonts.small, i18n_text(STR_CALENDAR_HINT)) <= line);
            CHECK(text_width(theme.fonts.title, i18n_text(STR_TIME_UNKNOWN)) <= line);
        }
    }
}

static bool toast_fits(const Theme *theme, const char *text)
{
    int room = theme->metrics.screen_width - 2 * theme->metrics.padding;
    return text_width(theme->fonts.body, text) <= room ||
           wraps_whole(theme->fonts.small, text, room, TOAST_MAX_LINES);
}

static void every_toast_is_shown_whole_at_every_size_and_orientation(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = themed(size, orientation);
            for (size_t index = 0; index < sizeof TOASTS / sizeof TOASTS[0]; index++) {
                CHECK(toast_fits(&theme, i18n_text(TOASTS[index])));
            }
            for (size_t index = 0; index < sizeof TOAST_PATTERNS / sizeof TOAST_PATTERNS[0];
                 index++) {
                CHECK(toast_fits(&theme, pattern_text(TOAST_PATTERNS[index])));
            }
        }
    }
}

static void every_home_item_name_and_its_toasts_fit_at_every_size_and_orientation(void)
{
    static const StringId TOAST_IDS[] = {STR_HOME_WAS_HIDDEN, STR_HOME_WAS_SHOWN, STR_HOME_PLACED};
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            Theme theme = themed(size, orientation);
            int room = header_title_bounds(&theme).width;
            for (int item = 0; item < HOME_ITEM_COUNT; item++) {
                const char *name = i18n_text(APP_HOME_ITEMS[item].label);
                CHECK(text_width(theme.fonts.header, name) <= room);
                for (int toast = 0; toast < 3; toast++) {
                    Pattern text = {TOAST_IDS[toast], {name}};
                    CHECK(toast_fits(&theme, pattern_text(text)));
                }
            }
        }
    }
}

static int export_height(const Theme *theme, const char *headline, const char *detail)
{
    int width = theme_content_area(theme).width - 2 * theme->metrics.padding;
    CHECK(wraps_whole(theme->fonts.title, headline, width, HINT_LINES));
    CHECK(wraps_whole(theme->fonts.body, detail, width, HINT_LINES));
    return label_wrap_count(theme->fonts.title, headline, width) *
               font_line_height(theme->fonts.title) +
           label_wrap_count(theme->fonts.body, detail, width) * font_line_height(theme->fonts.body);
}

static void every_export_status_fits_its_screen_at_every_size_and_orientation(void)
{
    size_t headlines = sizeof EXPORT_HEADLINES / sizeof EXPORT_HEADLINES[0];
    size_t details = sizeof EXPORT_DETAILS / sizeof EXPORT_DETAILS[0];
    for (int variant = 0; variant < 2 * TEXT_SIZE_COUNT; variant++) {
        Theme theme = themed(variant % TEXT_SIZE_COUNT, variant / TEXT_SIZE_COUNT);
        Rect content = theme_content_area(&theme);
        int room = content.height - theme.metrics.padding - font_line_height(theme.fonts.small);
        int width = content.width - 2 * theme.metrics.padding;
        CHECK(text_width(theme.fonts.small, i18n_text(STR_BACK_CANCELS)) <= width);
        for (size_t headline = 0; headline < headlines; headline++) {
            char shown[TEXT_CAPACITY];
            memcpy(shown, pattern_text(EXPORT_HEADLINES[headline]), TEXT_CAPACITY);
            for (size_t detail = 0; detail < details; detail++) {
                const char *text = pattern_text(EXPORT_DETAILS[detail]);
                CHECK(export_height(&theme, shown, text) + 8 <= room);
            }
        }
    }
}

static void a_toast_taller_than_one_line_stays_on_screen(void)
{
    Theme theme = themed(TEXT_SIZE_LARGE, ORIENTATION_PORTRAIT);
    Toast toast;
    toast_show(&toast, i18n_text(STR_SCAN_NO_CODE), 0);
    Rect area = toast_area(&toast, &theme);
    CHECK(area.height > theme.metrics.row_height);
    CHECK(area.y > theme.metrics.header_height);
    CHECK(area.y + area.height <= theme.metrics.screen_height);
}

static void every_bluetooth_hint_fits_three_lines_in_portrait_and_one_in_landscape(void)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        Theme theme = themed(TEXT_SIZE_MEDIUM, orientation);
        int room = theme.metrics.screen_width - 2 * theme.metrics.padding;
        for (size_t index = 0; index < sizeof BLUETOOTH_HINTS / sizeof BLUETOOTH_HINTS[0];
             index++) {
            const char *text = i18n_text(BLUETOOTH_HINTS[index]);
            CHECK(wraps_whole(theme.fonts.small, text, room, orientation == 0 ? 1 : HINT_LINES));
        }
    }
}

int main(void)
{
    i18n_detach();
    static const TestCase cases[] = {
        TEST_CASE(every_notice_title_fits_the_header_in_both_orientations),
        TEST_CASE(every_notice_message_wraps_whole_into_the_notice_in_both_orientations),
        TEST_CASE(every_screen_title_fits_the_header_at_every_size_and_orientation),
        TEST_CASE(every_empty_screen_message_wraps_whole_at_every_size_and_orientation),
        TEST_CASE(every_month_title_and_clock_hint_fits_at_every_size_and_orientation),
        TEST_CASE(every_toast_is_shown_whole_at_every_size_and_orientation),
        TEST_CASE(every_home_item_name_and_its_toasts_fit_at_every_size_and_orientation),
        TEST_CASE(a_toast_taller_than_one_line_stays_on_screen),
        TEST_CASE(every_export_status_fits_its_screen_at_every_size_and_orientation),
        TEST_CASE(every_bluetooth_hint_fits_three_lines_in_portrait_and_one_in_landscape),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
