#include "device/sd_card.h"
#include "mmc.h"
#include "sd.h"

uint32_t sd_allocation_unit(void)
{
    uint32_t response[4];
    uint32_t status[16];
    SdCommand app = {55, sd_card_rca_argument(sd_card_info()), SD_RESPONSE_SHORT, false};
    if (!sd_card_ready() || !mmc_command(&SD_MMC_HOST, app, response) ||
        !mmc_read_sd_status(&SD_MMC_HOST, status)) {
        return sd_card_info()->erase_sectors;
    }
    static const uint32_t units[] = {0,    32,   64,    128,   256,   512,   1024,  2048,
                                     4096, 8192, 16384, 24576, 32768, 49152, 65536, 131072};
    uint32_t unit = units[((const uint8_t *)status)[10] >> 4];
    return unit != 0 ? unit : sd_card_info()->erase_sectors;
}
