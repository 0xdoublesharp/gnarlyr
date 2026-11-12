#include "sd.h"

#include <stddef.h>

#include "board.h"
#include "cru.h"
#include "device/card_presence.h"
#include "device/card_write.h"
#include "device/sd_card.h"
#include "gpio.h"
#include "hal/clock.h"
#include "mmc.h"
#include "registers.h"

#define CRU_SD_IDENTIFY 0x009F009Fu
#define CRU_SD_TRANSFER 0x009F0080u
#define GRF_SD_MUX_LOW 0x20u
#define GRF_SD_MUX_LOW_VALUE 0xFC005400u
#define GRF_SD_MUX_HIGH 0x24u
#define GRF_SD_MUX_HIGH_VALUE 0x003F0015u
#define GRF_DETECT_MUX 0x04u
#define GRF_DETECT_MUX_VALUE 0x00C00000u
#define GRF_DETECT_PULL 0x14u
#define GRF_DETECT_PULL_VALUE 0x00080008u
#define CRU_MMC_DIVIDERS 0x30u
#define STATUS_READY_FOR_DATA (1u << 8)
#define STATUS_STATE_SHIFT 9
#define STATUS_STATE_MASK 0xFu
#define STATE_TRANSFER 4u

enum {
    GATE_SD_BUS = 0x18,
    GATE_SD_CARD_CLOCK = 0x6B,
    RESET_SD = 0x12,
    CMD_SEND_STATUS = 13,
    STATUS_TRIES = 1000,
    TRANSFER_READY_TIMEOUT_MS = 1000,
    POWER_SETTLE_MS = 1,
    DETECT_SETTLE_MS = 1
};

static const GpioPin CARD_DETECT = {0, 11};
static uint32_t sd_fifo_depth;
static const MmcHost SD_HOST = {SD_BASE, CRU_SD_IDENTIFY, CRU_SD_TRANSFER, &sd_fifo_depth};

static SdCard card;
static bool ready;
static bool tried;
static bool detect_started;
static CardPresence presence;
static uint32_t generation;

static bool host_command(void *context, SdCommand command, uint32_t response[4])
{
    (void)context;
    return mmc_command(&SD_HOST, command, response);
}

static void host_set_clock(void *context, SdClock clock)
{
    (void)context;
    mmc_set_clock(&SD_HOST, clock);
}

static void host_set_bus_width(void *context, uint8_t bits)
{
    (void)context;
    mmc_set_bus_width(&SD_HOST, bits);
}

static void host_delay(void *context, uint32_t milliseconds)
{
    (void)context;
    board_delay_ms(milliseconds);
}

static bool host_card_present(void *context)
{
    (void)context;
    return !gpio_read(CARD_DETECT);
}

static void host_stop(void *context)
{
    (void)context;
    cru_gate(GATE_SD_CARD_CLOCK, true);
    mmc_stop_clock(&SD_HOST);
    cru_gate(GATE_SD_CARD_CLOCK, false);
    cru_gate(GATE_SD_BUS, false);
}

static const SdHost HOST = {host_command, host_set_clock,    host_set_bus_width,
                            host_delay,   host_card_present, host_stop,
                            NULL};

static void start_card_detect(void)
{
    if (detect_started) {
        return;
    }
    detect_started = true;
    register_write(GRF_BASE + GRF_DETECT_MUX, GRF_DETECT_MUX_VALUE);
    gpio_input(CARD_DETECT);
    register_write(GRF_BASE + GRF_DETECT_PULL, GRF_DETECT_PULL_VALUE);
    board_delay_ms(DETECT_SETTLE_MS);
    presence.candidate = host_card_present(NULL);
    presence.since_ms = hal_clock_milliseconds();
    board_delay_ms(CARD_PRESENCE_STEADY_MS);
}

static void power_controller(void)
{
    cru_gate(GATE_SD_BUS, true);
    cru_gate(GATE_SD_CARD_CLOCK, true);
    cru_reset(RESET_SD, false);
    cru_write(CRU_MMC_DIVIDERS, CRU_SD_IDENTIFY);
    register_write(GRF_BASE + GRF_SD_MUX_LOW, GRF_SD_MUX_LOW_VALUE);
    register_write(GRF_BASE + GRF_SD_MUX_HIGH, GRF_SD_MUX_HIGH_VALUE);
    board_delay_ms(POWER_SETTLE_MS);
    mmc_reset_controller(&SD_HOST);
    mmc_set_clock(&SD_HOST, SD_CLOCK_IDENTIFY);
    board_delay_ms(POWER_SETTLE_MS);
}

bool sd_read(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    (void)context;
    if (!sd_present() || presence.ejected || !ready || count == 0 || lba >= card.sector_count ||
        count > card.sector_count - lba) {
        return false;
    }
    return mmc_read_blocks(&SD_HOST, sd_card_address(&card, lba), count,
                           sd_card_rca_argument(&card), buffer);
}

static bool start_card(void)
{
    start_card_detect();
    if (!host_card_present(NULL)) {
        return false;
    }
    power_controller();
    return sd_card_init(&card, &HOST);
}

bool sd_start(BlockDevice *device)
{
    *device = (BlockDevice){sd_read, NULL, sd_write};
    if (!sd_present() || presence.ejected) {
        return false;
    }
    if (!tried) {
        tried = true;
        ready = start_card();
    }
    return ready;
}

static bool wait_for_transfer_state(void)
{
    uint32_t words[4] = {0, 0, 0, 0};
    SdCommand status = {CMD_SEND_STATUS, sd_card_rca_argument(&card), SD_RESPONSE_SHORT, false};
    uint32_t started_ms = hal_clock_milliseconds();
    for (int attempt = 0; attempt < STATUS_TRIES; attempt++) {
        if (!sd_present() || hal_clock_milliseconds() - started_ms >= TRANSFER_READY_TIMEOUT_MS) {
            return false;
        }
        if (mmc_command(&SD_HOST, status, words) && (words[0] & STATUS_READY_FOR_DATA) != 0 &&
            ((words[0] >> STATUS_STATE_SHIFT) & STATUS_STATE_MASK) == STATE_TRANSFER) {
            return true;
        }
    }
    return false;
}

static bool write_data(void *context, uint8_t command, uint32_t address, uint32_t count,
                       const uint32_t *words)
{
    (void)context;
    return mmc_write_blocks(&SD_HOST, command, address, count, words);
}

static bool stop_writing(void *context)
{
    (void)context;
    return sd_present() && mmc_stop(&SD_HOST, sd_card_rca_argument(&card));
}

static bool wait_until_ready(void *context)
{
    (void)context;
    if (wait_for_transfer_state()) {
        return true;
    }
    mmc_note_failure(&SD_HOST, MMC_STEP_TRANSFER_STATE);
    return false;
}

static const CardWriter WRITER = {write_data, stop_writing, wait_until_ready, NULL};

bool sd_write(void *context, uint32_t lba, uint32_t count, const void *buffer)
{
    (void)context;
    if (!sd_present() || presence.ejected || !ready || lba >= card.sector_count ||
        count > card.sector_count - lba) {
        return false;
    }
    return card_write(&WRITER, sd_card_address(&card, lba), count, buffer);
}

uint32_t sd_sector_count(void)
{
    return ready ? card.sector_count : 0;
}

bool sd_present(void)
{
    start_card_detect();
    bool raw = host_card_present(NULL);
    bool before = presence.present;
    bool present = card_presence_update(&presence, raw, hal_clock_milliseconds());
    if (present != before) {
        generation++;
        ready = false;
        tried = false;
        if (!present) {
            presence.ejected = false;
        }
    }
    return present && raw;
}

bool sd_ejected(void)
{
    return presence.ejected;
}

void sd_eject(bool eject)
{
    presence.ejected = eject;
    sd_forget();
    generation++;
}

uint32_t sd_generation(void)
{
    return generation;
}

void sd_forget(void)
{
    ready = false;
    tried = false;
}

uint32_t sd_allocation_unit(void)
{
    uint32_t response[4];
    uint32_t status[16];
    SdCommand app = {55, sd_card_rca_argument(&card), SD_RESPONSE_SHORT, false};
    if (!ready || !mmc_command(&SD_HOST, app, response) || !mmc_read_sd_status(&SD_HOST, status)) {
        return card.erase_sectors;
    }
    static const uint32_t units[] = {0,    32,   64,    128,   256,   512,   1024,  2048,
                                     4096, 8192, 16384, 24576, 32768, 49152, 65536, 131072};
    uint32_t unit = units[((const uint8_t *)status)[10] >> 4];
    return unit != 0 ? unit : card.erase_sectors;
}
