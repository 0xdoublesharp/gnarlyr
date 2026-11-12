#include "usb_session.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "debug_probe.h"
#include "device/fat.h"
#include "device/update_names.h"
#include "device/update_trial.h"
#include "device/updater.h"
#include "emmc.h"
#include "hal/power.h"
#include "internal_volume.h"
#include "trial_module.h"

enum {
    MESSAGE_CAPACITY = 96,
    PERCENT_STEP = 5,
    PERCENT = 100,
    RESTART_HOLD_MS = 2000,
    BOTH_SLOTS_TEXT_BYTES = 10
};

static char update_path[UPDATE_NAME_CAPACITY];
static StringId install_title;

static FatVolume *volume;
static FatFile file;
static uint32_t last_percent;
static UsbShared *shared;

static bool read_update(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    return fat_read(volume, &file, offset, buffer, length);
}

static void show_percent(uint32_t percent)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_UPDATE_PROGRESS, percent, 0);
    app_show_notice(i18n_text(install_title), text);
    board_display_service();
}

static void report(void *context, uint32_t done, uint32_t total)
{
    (void)context;
    uint32_t percent = done * PERCENT / total;
    if (percent >= last_percent + PERCENT_STEP) {
        last_percent = percent;
        show_percent(percent);
    }
}

static void remove_update_file(void)
{
    fat_delete(volume, update_path);
}

static bool open_highest_numbered(void)
{
    for (uint32_t index = 0; index < update_name_count(); index++) {
        if (update_name_at(index, update_path) && fat_open(volume, update_path, &file)) {
            return true;
        }
    }
    return false;
}

static bool mount_volume(EmmcInfo *info)
{
    if (!emmc_start(info)) {
        return false;
    }
    volume = internal_volume_remount();
    return volume != NULL;
}

static bool open_update(UpdateStorage *storage)
{
    EmmcInfo info;
    if (!mount_volume(&info) || !open_highest_numbered()) {
        return false;
    }
    *storage = (UpdateStorage){emmc_read,          emmc_write,         NULL,
                               info.layout.slot_1, info.layout.slot_2, info.layout.slot_sectors};
    return true;
}

static bool both_slots_requested(void)
{
    static const char TEXT[BOTH_SLOTS_TEXT_BYTES] = {'B', 'O', 'T', 'H', ' ',
                                                     'S', 'L', 'O', 'T', 'S'};
    char bytes[BOTH_SLOTS_TEXT_BYTES];
    FatFile request;
    if (!fat_open(volume, UPDATE_BOTH_SLOTS_PATH, &request) ||
        request.size < BOTH_SLOTS_TEXT_BYTES ||
        !fat_read(volume, &request, 0, bytes, sizeof bytes)) {
        return false;
    }
    for (uint32_t index = 0; index < BOTH_SLOTS_TEXT_BYTES; index++) {
        if (bytes[index] != TEXT[index]) {
            return false;
        }
    }
    return true;
}

static bool write_slots(const UpdateFile *update, bool stock)
{
    const TrialCalls *trial = board_trial_calls();
    bool both = stock || both_slots_requested();
    bool installed = trial != NULL && trial->install(update, usb_session_buffer(),
                                                     (UpdateProgress){report, NULL}, both);
    if (installed && both) {
        fat_delete(volume, UPDATE_BOTH_SLOTS_PATH);
    }
    return installed;
}

static void install(const UpdateFile *update, UpdateCheck check)
{
    bool stock = check == UPDATE_STOCK;
    install_title = stock ? STR_UPDATE_INSTALLING_STOCK : STR_UPDATE_INSTALLING;
    last_percent = 0;
    show_percent(0);
    shared->flash_rewritten = true;
    bool installed = write_slots(update, stock);
    if (updater_should_remove_file(check, installed)) {
        remove_update_file();
    }
    if (!installed) {
        app_show_notice(i18n_text(STR_UPDATE_FAILED), i18n_text(STR_UPDATE_FAILED_HINT));
        board_wait_for_key();
        return;
    }
    debug_restart_after_install(volume, shared);
    const char *title = i18n_text(stock ? STR_UPDATE_INSTALLED_STOCK : STR_UPDATE_INSTALLED);
    app_show_notice(title, i18n_text(STR_UPDATE_UNPLUG));
    do {
        board_wait_for_power_hold(RESTART_HOLD_MS);
        app_show_notice(title, i18n_text(STR_UPDATE_UNPLUG_FIRST));
    } while (hal_power_battery().usb_power);
    app_show_notice(title, i18n_text(STR_UPDATE_TURNING_OFF));
    board_display_service();
    hal_power_off();
}

static void show_refused(const char *name)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_text(&builder, STR_UPDATE_REFUSED, name);
    app_show_notice(i18n_text(STR_UPDATE_NOT_INSTALLED), text);
    board_wait_for_key();
}

void update_after_usb(UsbShared *state)
{
    UpdateStorage storage;
    shared = state;
    if (!open_update(&storage)) {
        return;
    }
    _Static_assert(UPDATER_SCRATCH_WORDS * 4 <= 64 * 512, "the USB write buffer holds the scratch");
    updater_use_scratch(usb_session_buffer());
    UpdateFile update = {read_update, file.size, NULL};
    const char *name = update_path + 1;
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_text(&builder, STR_UPDATE_CHECKING, name);
    app_show_notice(i18n_text(STR_UPDATE), text);
    UpdateCheck check = updater_check(&update, &storage);
    if (check == UPDATE_VALID || check == UPDATE_STOCK) {
        install(&update, check);
        return;
    }
    if (updater_should_remove_file(check, false)) {
        remove_update_file();
    }
    if (check == UPDATE_INVALID) {
        show_refused(name);
    }
}
