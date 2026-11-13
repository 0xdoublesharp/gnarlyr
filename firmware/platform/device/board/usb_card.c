#include "usb_card.h"

#include <stddef.h>

#include "device/block_device.h"
#include "device/card_write.h"
#include "device/emmc_layout.h"
#include "device/pll.h"
#include "device/sd_card.h"
#include "emmc.h"
#include "hal/clock.h"
#include "mmc.h"
#include "mmc_registers.h"
#include "registers.h"
#include "sd.h"

#define INT_WRITE_ERRORS 0xAB82u
#define INT_WRITE_FAILED 0xAA80u
#define STATUS_READY_FOR_DATA (1u << 8)
#define STATUS_STATE_SHIFT 9
#define STATUS_STATE_MASK 0xFu
#define STATE_TRANSFER 4u
#define EMMC_RCA_ARGUMENT 0x00020000u
#define STATUS_SWITCH_ERROR (1u << 7)
#define SWITCH_HIGH_SPEED_TIMING 0x03B90100u
#define SD_CHECK_HIGH_SPEED 0x00FFFFF1u
#define SD_SWITCH_HIGH_SPEED 0x80FFFFF1u
#define CARD_TYPE_52_MHZ 0x02u
#define SD_GROUP_1_HIGH_SPEED 0x02u
#define CRU_PLL_CON0 (CRU_BASE + 0x00u)
#define CRU_PLL_CON1 (CRU_BASE + 0x04u)
#define CRU_MODE (CRU_BASE + 0x10u)
#define CRU_MODE_PLL 0x1u
#define SD_FAST_SOURCE 0x009F0000u
#define EMMC_FAST_SOURCE 0x9F000000u

enum {
    CMD_STOP_TRANSMISSION = 12,
    CMD_SEND_STATUS = 13,
    CMD_SET_BLOCK_COUNT = 23,
    CMD_APP = 55,
    CMD_SWITCH = 6,
    CMD_SEND_EXT_CSD = 8,
    EXT_CSD_CARD_TYPE = 196,
    SD_SWITCH_BYTES = 64,
    SD_SUPPORT_BYTE = 13,
    SD_RESULT_BYTE = 16,
    SD_WIDE_BUS = 4,
    EMMC_DIVIDER_SHIFT = 8,
    FAST_SOURCE_LIMIT_HZ = 200000000,
    FAST_DIVIDER = 4,
    BLOCK_BYTES = 512,
    COMMAND_TIMEOUT_MS = 100,
    DATA_TIMEOUT_MS = 500,
    BUSY_TIMEOUT_MS = 1000,
    READY_TIMEOUT_MS = 1000
};

typedef struct UsbCard {
    const MmcHost *host;
    uint32_t rca;
    uint32_t moved_ms;
    CardStream stream;
    bool sd;
    bool stalled;
    bool fast;
} UsbCard;

static UsbCard cards[USB_CARD_COUNT];
static uint8_t fallbacks;
static const EmmcLayout *layout;

static uint32_t read_mmc(const UsbCard *card, uint32_t offset)
{
    return register_read(card->host->base + offset);
}

static void write_mmc(const UsbCard *card, uint32_t offset, uint32_t value)
{
    register_write(card->host->base + offset, value);
}

static bool settled(const UsbCard *card, uint32_t offset, uint32_t bits, uint32_t timeout_ms)
{
    uint32_t start = hal_clock_milliseconds();
    while ((read_mmc(card, offset) & bits) != 0) {
        if (hal_clock_milliseconds() - start > timeout_ms) {
            return false;
        }
    }
    return true;
}

static bool command(const UsbCard *card, uint8_t index, uint32_t argument)
{
    uint32_t response[4];
    SdCommand sent = {index, argument, SD_RESPONSE_SHORT, false};
    return mmc_command(card->host, sent, response);
}

static bool pre_count(void *context, uint32_t count)
{
    const UsbCard *card = context;
    if (card->sd && !command(card, CMD_APP, card->rca)) {
        return false;
    }
    return command(card, CMD_SET_BLOCK_COUNT, count);
}

static bool start_data(const UsbCard *card, uint32_t command, uint32_t argument, uint32_t bytes)
{
    if (!settled(card, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS)) {
        return false;
    }
    write_mmc(card, MMC_CTRL, read_mmc(card, MMC_CTRL) | CTRL_FIFO_RESET);
    if (!settled(card, MMC_CTRL, CTRL_FIFO_RESET, COMMAND_TIMEOUT_MS)) {
        return false;
    }
    write_mmc(card, MMC_BLKSIZ, bytes < BLOCK_BYTES ? bytes : BLOCK_BYTES);
    write_mmc(card, MMC_BYTCNT, bytes);
    write_mmc(card, MMC_RINTSTS, INT_ALL);
    write_mmc(card, MMC_CMDARG, argument);
    write_mmc(card, MMC_CMD, CMD_BASE | CMD_DATA_READ | command);
    return settled(card, MMC_CMD, CMD_START, COMMAND_TIMEOUT_MS);
}

static bool begin(void *context, uint8_t index, uint32_t address, uint32_t count)
{
    UsbCard *card = context;
    card->stalled = false;
    return start_data(card, CMD_WRITE | index, address, BLOCK_BYTES * count);
}

static bool feed(void *context, const uint32_t *words, uint32_t count, uint32_t *accepted)
{
    UsbCard *card = context;
    if ((read_mmc(card, MMC_RINTSTS) & INT_WRITE_ERRORS) != 0) {
        return false;
    }
    uint32_t level =
        (read_mmc(card, MMC_STATUS) >> STATUS_FIFO_COUNT_SHIFT) & STATUS_FIFO_COUNT_MASK;
    uint32_t depth = *card->host->fifo_depth;
    uint32_t space = level < depth ? depth - level : 0;
    uint32_t sent = 0;
    for (; sent < space && sent < count; sent++) {
        write_mmc(card, MMC_FIFO, words[sent]);
    }
    *accepted = sent;
    if (sent > 0) {
        card->stalled = false;
        return true;
    }
    uint32_t now = hal_clock_milliseconds();
    if (!card->stalled) {
        card->stalled = true;
        card->moved_ms = now;
    }
    return now - card->moved_ms <= DATA_TIMEOUT_MS;
}

static bool end(void *context)
{
    const UsbCard *card = context;
    uint32_t start = hal_clock_milliseconds();
    while ((read_mmc(card, MMC_RINTSTS) & (INT_DATA_OVER | INT_WRITE_FAILED)) == 0) {
        if (hal_clock_milliseconds() - start > DATA_TIMEOUT_MS) {
            return false;
        }
    }
    bool clean = (read_mmc(card, MMC_RINTSTS) & INT_WRITE_FAILED) == 0;
    write_mmc(card, MMC_RINTSTS, INT_ALL);
    return clean;
}

static bool stop(void *context)
{
    const UsbCard *card = context;
    write_mmc(card, MMC_RINTSTS, INT_ALL);
    write_mmc(card, MMC_CMDARG, card->rca);
    write_mmc(card, MMC_CMD,
              CMD_BASE | CMD_STOP_ABORT | CMD_RESPONSE | CMD_CHECK_CRC | CMD_STOP_TRANSMISSION);
    uint32_t start = hal_clock_milliseconds();
    while ((read_mmc(card, MMC_RINTSTS) & INT_COMMAND_DONE) == 0) {
        if (hal_clock_milliseconds() - start > COMMAND_TIMEOUT_MS) {
            return false;
        }
    }
    uint32_t errors = INT_RESPONSE_ERROR | INT_RESPONSE_TIMEOUT | INT_RESPONSE_CRC;
    bool answered = (read_mmc(card, MMC_RINTSTS) & errors) == 0;
    write_mmc(card, MMC_RINTSTS, INT_ALL);
    return settled(card, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS) && answered;
}

static uint32_t ready_status(const UsbCard *card)
{
    uint32_t response[4] = {0, 0, 0, 0};
    SdCommand status = {CMD_SEND_STATUS, card->rca, SD_RESPONSE_SHORT, false};
    uint32_t start = hal_clock_milliseconds();
    while (hal_clock_milliseconds() - start < READY_TIMEOUT_MS) {
        if (mmc_command(card->host, status, response) &&
            (response[0] & STATUS_READY_FOR_DATA) != 0 &&
            ((response[0] >> STATUS_STATE_SHIFT) & STATUS_STATE_MASK) == STATE_TRANSFER) {
            return response[0];
        }
    }
    return 0;
}

static bool wait_ready(void *context)
{
    return ready_status(context) != 0;
}

static bool fetch(UsbCard *card, uint32_t command, uint32_t argument, uint32_t bytes,
                  uint32_t *words)
{
    if (!start_data(card, command, argument, bytes)) {
        return false;
    }
    uint32_t start = hal_clock_milliseconds();
    for (uint32_t taken = 0; taken < bytes / 4;) {
        if ((read_mmc(card, MMC_RINTSTS) & INT_WRITE_FAILED) != 0 ||
            hal_clock_milliseconds() - start > DATA_TIMEOUT_MS) {
            return false;
        }
        uint32_t level =
            (read_mmc(card, MMC_STATUS) >> STATUS_FIFO_COUNT_SHIFT) & STATUS_FIFO_COUNT_MASK;
        for (; level > 0 && taken < bytes / 4; level--) {
            words[taken++] = read_mmc(card, MMC_FIFO);
        }
    }
    return end(card);
}

static const CardStreamOps SD_OPS = {pre_count,           begin, feed, end, stop, wait_ready,
                                     &cards[USB_CARD_SD], false};
static const CardStreamOps EMMC_OPS = {
    pre_count, begin, feed, end, stop, wait_ready, &cards[USB_CARD_INTERNAL], true};

static bool locate_sd(UsbCard *card, uint32_t lba, uint32_t count, uint32_t *address)
{
    BlockDevice device;
    uint32_t sectors = sd_sector_count();
    if (!sd_start(&device) || lba >= sectors || count > sectors - lba) {
        return false;
    }
    const SdCard *info = sd_card_info();
    card->rca = sd_card_rca_argument(info);
    *address = sd_card_address(info, lba);
    return true;
}

static bool locate_internal(uint32_t lba, uint32_t count, uint32_t *address)
{
    *address = layout->volume_start + lba;
    return emmc_layout_in_volume(layout, lba, count) &&
           emmc_layout_allows_write(layout, *address, count);
}

static bool open_stream(void *context, uint32_t lba, uint32_t count)
{
    UsbCard *card = &cards[(uintptr_t)context];
    uint32_t address = 0;
    bool found =
        card->sd ? locate_sd(card, lba, count, &address) : locate_internal(lba, count, &address);
    card->stream.ops = NULL;
    return found && card_stream_open(&card->stream, card->sd ? &SD_OPS : &EMMC_OPS, address, count);
}

static bool feed_stream(void *context, const uint32_t *words, uint32_t count, uint32_t *accepted)
{
    UsbCard *card = &cards[(uintptr_t)context];
    return card_stream_feed(&card->stream, words, count, accepted);
}

static bool close_stream(void *context, bool complete)
{
    UsbCard *card = &cards[(uintptr_t)context];
    bool closed = card->stream.ops != NULL && card_stream_close(&card->stream, complete);
    if (!closed) {
        usb_card_slow_down((uint8_t)(uintptr_t)context);
    }
    return closed;
}

const MscStream USB_CARD_STREAM = {open_stream, feed_stream, close_stream};

void usb_card_prepare(const EmmcLayout *internal)
{
    layout = internal;
    cards[USB_CARD_INTERNAL] = (UsbCard){.host = &EMMC_HOST, .rca = EMMC_RCA_ARGUMENT};
    cards[USB_CARD_SD] = (UsbCard){.host = &SD_MMC_HOST, .sd = true};
}

static bool source_fits(void)
{
    if ((register_read(CRU_MODE) & CRU_MODE_PLL) == 0) {
        return false;
    }
    uint32_t hz = pll_output_hz(register_read(CRU_PLL_CON0), register_read(CRU_PLL_CON1));
    return hz != 0 && hz <= FAST_SOURCE_LIMIT_HZ;
}

static void set_speed(UsbCard *card, bool fast)
{
    MmcHost host = *card->host;
    if (fast) {
        host.cru_transfer = card->sd ? SD_FAST_SOURCE | (FAST_DIVIDER - 1u)
                                     : EMMC_FAST_SOURCE | (FAST_DIVIDER - 1u) << EMMC_DIVIDER_SHIFT;
    }
    mmc_set_clock(&host, SD_CLOCK_TRANSFER);
    card->fast = fast;
}

static bool emmc_switched(UsbCard *card, uint32_t *scratch)
{
    uint32_t response[4];
    SdCommand timing = {CMD_SWITCH, SWITCH_HIGH_SPEED_TIMING, SD_RESPONSE_SHORT_BUSY, false};
    if (!fetch(card, CMD_SEND_EXT_CSD, 0, BLOCK_BYTES, scratch) ||
        (((const uint8_t *)scratch)[EXT_CSD_CARD_TYPE] & CARD_TYPE_52_MHZ) == 0 ||
        !mmc_command(card->host, timing, response)) {
        return false;
    }
    uint32_t status = ready_status(card);
    return status != 0 && (status & STATUS_SWITCH_ERROR) == 0;
}

static bool sd_switched(UsbCard *card, uint32_t *scratch)
{
    const SdCard *info = sd_card_info();
    const uint8_t *bytes = (const uint8_t *)scratch;
    if (!sd_card_ready() || info->bus_width != SD_WIDE_BUS ||
        !fetch(card, CMD_SWITCH, SD_CHECK_HIGH_SPEED, SD_SWITCH_BYTES, scratch) ||
        (bytes[SD_SUPPORT_BYTE] & SD_GROUP_1_HIGH_SPEED) == 0) {
        return false;
    }
    card->rca = sd_card_rca_argument(info);
    return fetch(card, CMD_SWITCH, SD_SWITCH_HIGH_SPEED, SD_SWITCH_BYTES, scratch) &&
           (bytes[SD_RESULT_BYTE] & 0x0Fu) == 1u;
}

void usb_card_speed_up(bool internal_ready, uint32_t *scratch)
{
    fallbacks = 0;
    if (!source_fits()) {
        return;
    }
    UsbCard *internal = &cards[USB_CARD_INTERNAL];
    UsbCard *card = &cards[USB_CARD_SD];
    if (internal_ready && emmc_switched(internal, scratch)) {
        set_speed(internal, true);
    }
    if (sd_switched(card, scratch)) {
        set_speed(card, true);
    }
}

bool usb_card_slow_down(uint8_t lun)
{
    UsbCard *card = &cards[lun];
    if (!card->fast) {
        return false;
    }
    set_speed(card, false);
    fallbacks = (uint8_t)(fallbacks < UINT8_MAX ? fallbacks + 1 : fallbacks);
    return true;
}

void usb_card_speed_down(void)
{
    for (uint8_t lun = 0; lun < USB_CARD_COUNT; lun++) {
        if (cards[lun].fast) {
            set_speed(&cards[lun], false);
        }
    }
}

UsbCardSpeed usb_card_speed(void)
{
    uint8_t fast =
        (uint8_t)((cards[USB_CARD_INTERNAL].fast ? 1u : 0u) | (cards[USB_CARD_SD].fast ? 2u : 0u));
    return (UsbCardSpeed){fast, fallbacks};
}
