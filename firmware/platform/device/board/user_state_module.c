#include "trial_module.h"

#include <stddef.h>

#include "board.h"
#include "core/seam.h"
#include "device/slot_copy.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "internal_volume.h"
#include "overlay.h"
#include "storage_reset.h"
#include "usb_session.h"
#include "user_state_fat.h"

#define USER_STATE_BUFFER_BASE                                                                     \
    (OVERLAY_WINDOW_BASE + OVERLAY_WINDOW_BYTES - SLOT_COPY_BUFFER_WORDS * 4)

CORE_SEAM bool user_state_module_settle(UsbShared *shared)
{
    bool changed = (shared->trial.settle & USB_SETTLE_CHANGED) != 0;
    shared->trial.settle = 0;
    FatVolume *internal = internal_volume_mount();
    FatVolume *music = hal_file_volume() == HAL_VOLUME_CARD ? board_music_volume() : internal;
    UserStateDisk disk = {internal, music};
    UserStateIo io = user_state_fat_io(&disk, (uint8_t *)(uintptr_t)USER_STATE_BUFFER_BASE,
                                       SLOT_COPY_BUFFER_WORDS * 4);
    return internal != NULL && user_state_run(&io, hal_system_version(), changed);
}
