#include "hal/storage.h"

#include "board.h"
#include "device/fat.h"
#include "sd.h"
#include "usb_session.h"

static FatVolume check_volume;

bool card_usb_busy(void)
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
                         .usb_busy = card_usb_busy(),
                         .mounted = hal_file_volume() == HAL_VOLUME_CARD,
                         .ejected = sd_ejected()};
}
