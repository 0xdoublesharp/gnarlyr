#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/storage.h"
#include "settings/user_state.h"
#include "settings_screens.h"

static SettingsScreens screens;
static Toast toast;
static int prepare_calls;
static int reload_calls;
static StringId reload_message;

static void prepare(void)
{
    prepare_calls++;
}

static void reload(StringId message)
{
    reload_calls++;
    reload_message = message;
}

static void reset(void)
{
    fake_hal_reset();
    i18n_detach();
    screens = (SettingsScreens){0};
    toast = (Toast){0};
    prepare_calls = 0;
    reload_calls = 0;
    reload_message = STRING_COUNT;
    screens.context =
        (SettingsContext){.toast = &toast, .format_prepare = prepare, .reload = reload};
    screens.theme = &BOARD_THEME;
}

static void write_backups(uint8_t newest)
{
    UserStateList list = {.newest = newest};
    strcpy(list.builds[0], "older-build");
    strcpy(list.builds[1], "newer-build");
    uint8_t bytes[USER_STATE_LIST_BYTES];
    user_state_list_encode(&list, bytes);
    CHECK(hal_internal_file_write(USER_STATE_LIST_PATH, bytes, sizeof bytes));
}

static void write_mark(void)
{
    uint8_t mark[USER_STATE_MARK_BYTES];
    user_state_mark("current-build", 0, mark);
    CHECK(hal_internal_file_write(USER_STATE_MARK_PATH, mark, sizeof mark));
}

static MenuItem open_backups(void)
{
    MenuItem item = settings_backup_item(&screens);
    CHECK(item.shown(item.shown_context));
    ScreenAction action = item.select(item.context);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &screens.backup_menu.screen);
    return item;
}

static void restore_is_hidden_without_a_valid_backup_list(void)
{
    reset();
    MenuItem item = settings_backup_item(&screens);
    CHECK(!item.shown(item.shown_context));
    CHECK(strcmp(item.label, i18n_text(STR_BACKUPS)) == 0);
}

static void backups_are_shown_newest_first_and_restore_the_requested_slot(void)
{
    reset();
    write_backups(1);
    write_mark();
    (void)open_backups();
    CHECK_EQUAL(2, screens.backup_menu.config.item_count);
    CHECK(strcmp(screens.backup_items[0].label, "newer-build") == 0);
    CHECK(strcmp(screens.backup_items[1].label, "older-build") == 0);

    ScreenAction action = screens.backup_items[0].select(screens.backup_items[0].context);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, action.type);
    CHECK_EQUAL(1, prepare_calls);
    CHECK_EQUAL(1, reload_calls);
    CHECK_EQUAL(STR_SETTINGS_RESTORED, reload_message);
    uint32_t size = 0;
    const uint8_t *mark = fake_hal_internal_file_bytes(USER_STATE_MARK_PATH, &size);
    CHECK_EQUAL(USER_STATE_MARK_BYTES, size);
    CHECK_EQUAL(2, mark[USER_STATE_MARK_REQUEST_AT]);
}

static void the_older_backup_requests_the_other_slot(void)
{
    reset();
    write_backups(1);
    write_mark();
    (void)open_backups();
    ScreenAction action = screens.backup_items[1].select(screens.backup_items[1].context);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, action.type);
    uint32_t size = 0;
    const uint8_t *mark = fake_hal_internal_file_bytes(USER_STATE_MARK_PATH, &size);
    CHECK_EQUAL(1, mark[USER_STATE_MARK_REQUEST_AT]);
}

static void a_restore_reloads_the_battery_model_with_the_other_saved_settings(void)
{
    reset();
    SettingsStore store = {.battery = {.loaded = true, .dirty = true, .habit = 600}};
    screens.context.store = &store;
    write_backups(1);
    write_mark();
    (void)open_backups();
    screens.backup_items[0].select(screens.backup_items[0].context);
    CHECK(!store.battery.loaded);
    CHECK(!store.battery.dirty);
    CHECK_EQUAL(0, store.battery.habit);
}

static void a_missing_or_unwritable_mark_reports_failure_without_reloading(void)
{
    for (int failure = 0; failure < 2; failure++) {
        reset();
        write_backups(1);
        if (failure != 0) {
            write_mark();
            fake_hal_fail_internal_writes(USER_STATE_MARK_PATH);
        }
        (void)open_backups();
        ScreenAction action = screens.backup_items[0].select(screens.backup_items[0].context);
        CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
        CHECK(toast.visible);
        CHECK(strcmp(toast.text, i18n_text(STR_RESTORE_FAILED)) == 0);
        CHECK_EQUAL(0, prepare_calls);
        CHECK_EQUAL(0, reload_calls);
    }
}

static void a_backup_removed_after_opening_cannot_request_an_invalid_slot(void)
{
    reset();
    write_backups(1);
    write_mark();
    (void)open_backups();
    screens.backups = (UserStateList){0};
    ScreenAction action = screens.backup_items[0].select(screens.backup_items[0].context);
    CHECK_EQUAL(SCREEN_ACTION_NONE, action.type);
    CHECK(toast.visible);
    CHECK_EQUAL(0, prepare_calls);
    CHECK_EQUAL(0, reload_calls);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(restore_is_hidden_without_a_valid_backup_list),
        TEST_CASE(backups_are_shown_newest_first_and_restore_the_requested_slot),
        TEST_CASE(the_older_backup_requests_the_other_slot),
        TEST_CASE(a_restore_reloads_the_battery_model_with_the_other_saved_settings),
        TEST_CASE(a_missing_or_unwritable_mark_reports_failure_without_reloading),
        TEST_CASE(a_backup_removed_after_opening_cannot_request_an_invalid_slot),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
