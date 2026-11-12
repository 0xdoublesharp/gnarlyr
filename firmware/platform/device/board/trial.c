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
#include "mmc.h"
#include "overlay.h"
#include "trial_module.h"
#include "usb.h"
#include "usb_session.h"

enum {
    SECTOR_WORDS = 512 / 4,
    ESCAPE_LADDER_KEY = 1u << KEY_VOLUME_UP,
    ESCAPE_SETTLE_MS = 40,
    ESCAPE_STEP_MS = 50,
    ESCAPE_STEPS = 40,
    DIAG_SNAPSHOT_WORDS = 18,
    DIAG_REPORT_WORDS = 4,
    DIAG_WORDS = 2 * DIAG_SNAPSHOT_WORDS,
    DIAG_AT = 2,
    KEEP_ATTEMPTS = 0xFF
};

#define RECORD_MAGIC 0x54555344u
#define RECORD_RUNNING 0x01000001u
#define RECORD_FIXED 0xFF0000FFu
#define RECORD_CHECK 0x5Bu
#define DIAG_MAGIC 0x47414944u

typedef struct TrialDiag {
    uint32_t magic;
    uint32_t words[DIAG_WORDS];
} TrialDiag;

static uint32_t power_since_ms;
__attribute__((section(".noinit"))) static TrialDiag diag;

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

static bool record_locate(UsbTrial *trial)
{
    FatFile record;
    FatVolume *volume = internal_volume_mount();
    return volume != NULL && fat_open(volume, UPDATE_TRIAL_PATH, &record) &&
           fat_file_sector(volume, &record, 0, &trial->record_lba);
}

static void record_image(uint32_t *words, uint32_t attempts)
{
    words[0] = RECORD_MAGIC;
    words[1] = RECORD_RUNNING | attempts << 8 | (RECORD_CHECK ^ attempts) << 16;
}

static bool record_running(const uint32_t *words)
{
    uint32_t attempts = words[1] >> 8 & 0xFFu;
    return words[0] == RECORD_MAGIC && (words[1] & RECORD_FIXED) == RECORD_RUNNING &&
           (words[1] >> 16 & 0xFFu) == (RECORD_CHECK ^ attempts);
}

static void diag_keep(uint32_t *sector)
{
    if (diag.magic == DIAG_MAGIC) {
        sector[DIAG_AT] = DIAG_MAGIC;
        for (uint32_t index = 0; index < DIAG_WORDS; index++) {
            sector[DIAG_AT + 1 + index] = diag.words[index];
        }
        diag.magic = 0;
    }
}

static bool count_start(UsbTrial *trial, uint32_t *sector)
{
    uint32_t attempts = sector[1] >> 8 & 0xFFu;
    if (!record_running(sector) || attempts >= UPDATE_TRIAL_LIMIT) {
        return false;
    }
    trial->attempts = (uint8_t)(attempts + 1);
    record_image(sector, trial->attempts);
    diag_keep(sector);
    trial->running = true;
    trial->charged = emmc_volume_write(NULL, trial->record_lba, 1, sector);
    trial->session_due = trial->charged;
    return true;
}

void board_trial_boot(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    uint32_t sector[SECTOR_WORDS];
    if (!record_locate(trial) || !emmc_volume_read(NULL, trial->record_lba, 1, sector) ||
        count_start(trial, sector)) {
        return;
    }
    const TrialBootModuleEntry *entry = boot_module();
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

__attribute__((noinline, noclone)) static void write_record(const UsbTrial *trial,
                                                            uint32_t attempts)
{
    uint32_t sector[SECTOR_WORDS];
    if (trial->record_lba != 0 && emmc_volume_read(NULL, trial->record_lba, 1, sector)) {
        if (attempts <= UPDATE_TRIAL_LIMIT + 1) {
            record_image(sector, attempts);
        }
        diag_keep(sector);
        emmc_volume_write(NULL, trial->record_lba, 1, sector);
    }
}

static uint32_t *diag_copy(uint32_t *to, const void *from, uint32_t words)
{
    const uint32_t *source = from;
    for (uint32_t index = 0; index < words; index++) {
        to[index] = source[index];
    }
    return to + words;
}

void board_trial_snapshot(uint32_t kind)
{
    UsbShared *shared = usb_session_shared();
    uint32_t *words = diag.words + kind * DIAG_SNAPSHOT_WORDS;
    if (!shared->trial.running) {
        return;
    }
    MmcFailure failure = mmc_last_failure();
    words[0] = hal_clock_milliseconds();
    words[1] = hal_system_clock_mhz() | (uint32_t)shared->high_speed_failed << 16;
    words = diag_copy(words + 2, &shared->report, DIAG_REPORT_WORDS);
    words = diag_copy(words, &shared->link, sizeof shared->link / 4);
    words = diag_copy(words, &failure, sizeof failure / 4);
    *words = (uint32_t)shared->last_session.end;
    diag.magic = DIAG_MAGIC;
    write_record(&shared->trial, KEEP_ATTEMPTS);
}

void board_trial_settle(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->charged && hal_clock_milliseconds() >= UPDATE_TRIAL_SETTLE_MS) {
        trial->charged = false;
        write_record(trial, trial->attempts - 1u);
    }
}

void board_trial_session_starting(void)
{
    UsbTrial *trial = &usb_session_shared()->trial;
    if (trial->session_due) {
        trial->session_due = false;
        write_record(trial, trial->charged ? trial->attempts + 1u : trial->attempts);
    }
    trial->charged = false;
}

void board_trial_session_ended(void)
{
    UsbShared *shared = usb_session_shared();
    bool confirmed = shared->trial.running && shared->last_session.end == HAL_USB_END_EJECTED;
    const TrialBootModuleEntry *entry = confirmed || shared->trial.promoting ? boot_module() : NULL;
    if (entry != NULL) {
        entry->session_end(shared, copy_buffer());
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
