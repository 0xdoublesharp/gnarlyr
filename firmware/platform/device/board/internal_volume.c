#include "internal_volume.h"

#include <stddef.h>

#include "device/block_device.h"
#include "emmc.h"

static FatVolume volume;
static bool tried;
static bool mounted;

FatVolume *internal_volume_mount(void)
{
    if (!tried) {
        tried = true;
        BlockDevice device;
        mounted = emmc_volume_start(&device) && fat_mount(&volume, device);
    }
    return mounted ? &volume : NULL;
}

FatVolume *internal_volume_remount(void)
{
    internal_volume_reset();
    return internal_volume_mount();
}

void internal_volume_reset(void)
{
    tried = false;
    mounted = false;
}
