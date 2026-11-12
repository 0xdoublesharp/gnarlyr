#include "trial.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "device/fat.h"
#include "device/key_ladder.h"
#include "device/update_trial.h"
#include "emmc.h"
#include "hal/clock.h"
#include "internal_volume.h"
#include "overlay.h"
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

void board_trial_boot(void)
{
    FatFile record;
    FatVolume *volume = internal_volume_mount();
    if (volume == NULL || !fat_open(volume, UPDATE_TRIAL_PATH, &record)) {
        return;
    }
    const UsbOverlayEntry *usb = board_usb_overlay_load();
    if (usb != NULL && usb->magic == USB_OVERLAY_MAGIC) {
        usb->trial_start(usb_session_shared());
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
    usb_session_shared()->trial.charged = false;
}

void board_trial_poll_power(uint32_t now_ms)
{
    if (!usb_session_shared()->trial.running || !board_power_key_down()) {
        power_since_ms = now_ms;
    } else if (now_ms - power_since_ms >= UPDATE_TRIAL_RESET_HOLD_MS) {
        board_restart();
    }
}
