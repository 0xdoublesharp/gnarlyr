#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    MMC_SD_HIGH_SPEED_MHZ = 50,
    MMC_CRYSTAL_SOURCE = 0x80,
    MMC_CRU_DIVIDER_MASK = 0x1F,
    MMC_CRU_FIELD_MASK = MMC_CRYSTAL_SOURCE | MMC_CRU_DIVIDER_MASK
};

static inline uint32_t mmc_clock_field(uint32_t pll_mhz, uint8_t limit_mhz)
{
    if (limit_mhz >= 50) {
        if (pll_mhz == 200) {
            return 3;
        }
        if (pll_mhz == 96) {
            return 1;
        }
    }
    return limit_mhz == 26 && pll_mhz == 200 ? 7 : MMC_CRYSTAL_SOURCE;
}

static inline uint8_t mmc_emmc_limit_mhz(uint8_t device_type)
{
    return (device_type & 2) != 0 ? 52 : (device_type & 1) != 0 ? 26 : 0;
}

static inline bool mmc_card_status_ok(uint32_t status)
{
    return (status & 0xFFF9A088u) == 0;
}

static inline bool mmc_transfer_ready(uint32_t status)
{
    return mmc_card_status_ok(status) && (status & 0x1F00u) == 0x900u;
}
