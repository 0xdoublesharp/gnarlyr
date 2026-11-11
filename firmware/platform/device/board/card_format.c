#include "hal/storage.h"

#include <stddef.h>

#include "board.h"
#include "device/card_format.h"
#include "device/fat.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "sd.h"
#include "storage_reset.h"
#include "usb_session.h"

static FatVolume check_volume;

typedef struct FormatProgress {
    HalCardProgress progress;
    void *context;
    uint32_t generation;
    uint32_t started_ms;
} FormatProgress;

static bool usb_busy(void)
{
    UsbShared *usb = usb_session_shared();
    return usb->reconnect.in_session || usb->reconnect.requested || usb->reconnect.forced ||
           board_usb_listening();
}

HalCardInfo hal_card_info(void)
{
    BlockDevice device;
    bool present = sd_present();
    bool ready = present && sd_start(&device);
    return (HalCardInfo){.present = present,
                         .sectors = ready ? sd_sector_count() : 0,
                         .generation = sd_generation(),
                         .needs_format = ready && !fat_mount(&check_volume, device),
                         .usb_busy = usb_busy()};
}

static bool can_continue(void *context, uint32_t done, uint32_t total)
{
    FormatProgress *report = context;
    board_stall_beat();
    board_display_service();
    HalBattery battery = hal_power_battery();
    return sd_present() && sd_generation() == report->generation && !usb_busy() &&
           (battery.usb_power || (battery.known && battery.percent > 30)) &&
           hal_clock_milliseconds() - report->started_ms < 120000 &&
           (report->progress == NULL || report->progress(report->context, done, total));
}

static bool format_with(CardFormat *work, HalCardInfo expected, HalCardProgress progress,
                        void *context)
{
    HalCardInfo card = hal_card_info();
    if (!card.present || card.sectors == 0 || card.usb_busy || card.sectors != expected.sectors ||
        card.generation != expected.generation ||
        !card_format_plan(&work->plan, card.sectors, sd_allocation_unit()) ||
        !sd_start(&work->device)) {
        return false;
    }
    FormatProgress report = {progress, context, card.generation, hal_clock_milliseconds()};
    work->progress = can_continue;
    work->context = &report;
    if (!can_continue(&report, 0, 1)) {
        return false;
    }
    storage_reset();
    bool success = card_format_run(work) && fat_mount(&check_volume, work->device);
    storage_reset();
    sd_forget();
    return success;
}

bool hal_card_format(HalCardInfo expected, HalCardProgress progress, void *context)
{
    CardFormat *work = board_audio_lend_format();
    if (work == NULL) {
        return false;
    }
    *work = (CardFormat){0};
    bool success = format_with(work, expected, progress, context);
    board_audio_return_format();
    return success;
}
