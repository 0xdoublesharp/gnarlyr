#include "update.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/string_builder.h"
#include "device/fat.h"
#include "device/loader_request.h"
#include "device/update_names.h"
#include "device/updater.h"
#include "emmc.h"

enum {
    MESSAGE_CAPACITY = 64,
    PERCENT_STEP = 5,
    PERCENT = 100
};

static char update_path[UPDATE_NAME_CAPACITY];
static const char *install_title;

static FatVolume volume;
static FatFile file;
static uint32_t last_percent;
static bool flash_rewritten;

bool hal_system_restart_pending(void)
{
    return flash_rewritten;
}

static bool read_update(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    return fat_read(&volume, &file, offset, buffer, length);
}

static void show_percent(uint32_t percent)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, "Don't unplug or turn off. ");
    string_builder_append_unsigned(&builder, percent);
    string_builder_append_char(&builder, '%');
    app_show_notice(install_title, text);
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
    fat_delete(&volume, update_path);
}

static bool open_highest_numbered(void)
{
    for (uint32_t index = 0; index < update_name_count(); index++) {
        if (update_name_at(index, update_path) && fat_open(&volume, update_path, &file)) {
            return true;
        }
    }
    return false;
}

static bool mount_volume(EmmcInfo *info)
{
    BlockDevice device = {emmc_volume_read, NULL, emmc_volume_write};
    return emmc_start(info) && fat_mount(&volume, device);
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

static bool request_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    return fat_read(&volume, &file, offset, buffer, length);
}

static bool request_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    return fat_write(&volume, &file, offset, bytes, length);
}

void update_check_loader_request(void)
{
    EmmcInfo info;
    if (!mount_volume(&info) || !fat_open(&volume, LOADER_REQUEST_PATH, &file)) {
        return;
    }
    LoaderRequestFile request = {request_read, request_write, file.size, NULL};
    if (loader_request_consume(&request)) {
        board_loader_reboot();
    }
}

static void install(const UpdateFile *update, const UpdateStorage *storage, UpdateCheck check)
{
    bool stock = check == UPDATE_STOCK;
    install_title = stock ? "Installing FiiO firmware" : "Installing update";
    last_percent = 0;
    show_percent(0);
    flash_rewritten = true;
    bool installed = updater_install(update, storage, (UpdateProgress){report, NULL});
    if (updater_should_remove_file(check, installed)) {
        remove_update_file();
    }
    if (installed) {
        app_show_notice(stock ? "FiiO firmware installed" : "Update installed",
                        "Unplug USB, then hold Power to turn off and on again.");
    } else {
        app_show_notice("Update failed", "Copy the file again and retry before turning off.");
    }
    board_wait_for_key();
}

static void show_refused(const char *name)
{
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, name);
    string_builder_append(&builder, " isn't a valid image for this player.");
    app_show_notice("Update not installed", text);
    board_wait_for_key();
}

void update_after_usb(void)
{
    UpdateStorage storage;
    if (!open_update(&storage)) {
        return;
    }
    UpdateFile update = {read_update, file.size, NULL};
    const char *name = update_path + 1;
    char text[MESSAGE_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, "Checking ");
    string_builder_append(&builder, name);
    app_show_notice("Update", text);
    UpdateCheck check = updater_check(&update, &storage);
    if (check == UPDATE_VALID || check == UPDATE_STOCK) {
        install(&update, &storage, check);
        return;
    }
    if (updater_should_remove_file(check, false)) {
        remove_update_file();
    }
    if (check == UPDATE_INVALID) {
        show_refused(name);
    }
}
