#include "trial.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "device/fat.h"
#include "device/key_ladder.h"
#include "device/slot_copy.h"
#include "device/update_trial.h"
#include "emmc.h"
#include "extension_entry.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "internal_volume.h"
#include "overlay.h"
#include "trial_module.h"
#include "usb.h"
#include "usb_session.h"

enum {
    SECTOR_WORDS = 512 / 4,
    ESCAPE_LADDER_KEY = 1u << KEY_VOLUME_UP,
    ESCAPE_SETTLE_MS = 40,
    ESCAPE_STEP_MS = 50,
    ESCAPE_STEPS = 40
};

static uint32_t power_since_ms;

void board_trial_escape_check(void)
{
    if (!usb_vbus_present()) {
        return;
    }
    board_delay_ms(ESCAPE_SETTLE_MS);
    for (uint32_t step = 0; step < ESCAPE_STEPS; step++) {
        if (!usb_vbus_present() || !board_power_key_down() ||
            board_input_held() != ESCAPE_LADDER_KEY) {
            return;
        }
        board_delay_ms(ESCAPE_STEP_MS);
    }
    board_loader_reboot();
}

const TrialCalls *board_trial_calls(void)
{
    const ExtensionModuleEntry *code = hal_system_extension_code();
    return code != NULL ? code->trial : NULL;
}

static uint32_t *copy_buffer(void)
{
    return (uint32_t *)(uintptr_t)(OVERLAY_WINDOW_BASE + OVERLAY_WINDOW_BYTES -
                                   SLOT_COPY_BUFFER_WORDS * 4);
}

static const TrialBootModuleEntry *boot_module(void)
{
    return hal_system_load_module(TRIAL_BOOT_MODULE_NUMBER);
}

static const TrialBootModuleEntry *module_for_record(void)
{
    FatFile record;
    FatVolume *volume = internal_volume_mount();
    return volume != NULL && fat_open(volume, UPDATE_TRIAL_PATH, &record) ? boot_module() : NULL;
}

void board_trial_boot(void)
{
    const TrialBootModuleEntry *entry = module_for_record();
    if (entry != NULL) {
        entry->start(usb_session_shared(), copy_buffer());
    }
}

void board_trial_report(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->rolled_back) {
        trial->rolled_back = false;
        app_show_notice(i18n_text(STR_UPDATE_ROLLED_BACK), i18n_text(STR_UPDATE_ROLLED_BACK_HINT));
        board_wait_for_key();
    }
}

void board_trial_settle(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->charged && hal_clock_milliseconds() >= UPDATE_TRIAL_SETTLE_MS) {
        uint32_t sector[SECTOR_WORDS] = {trial->settled[0], trial->settled[1]};
        trial->charged = false;
        emmc_volume_write(NULL, trial->record_lba, 1, sector);
    }
}

void board_trial_session_starting(void)
{
    const TrialBootModuleEntry *entry = module_for_record();
    usb_session_shared()->trial.charged = false;
    if (entry != NULL) {
        entry->session_start();
    }
}

void board_trial_session_ended(void)
{
    const TrialBootModuleEntry *entry = module_for_record();
    if (entry != NULL) {
        entry->session_end(usb_session_shared(), copy_buffer());
    }
}

void board_trial_poll_power(uint32_t now_ms)
{
    if (!usb_session_shared()->trial.running || !board_power_key_down()) {
        power_since_ms = now_ms;
    } else if (now_ms - power_since_ms >= UPDATE_TRIAL_RESET_HOLD_MS) {
        board_restart();
    }
}
