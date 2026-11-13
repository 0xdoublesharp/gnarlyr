#include "usb_card.h"

#include <stddef.h>

#include "device/block_device.h"
#include "device/card_write.h"
#include "device/emmc_layout.h"
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

enum {
    CMD_STOP_TRANSMISSION = 12,
    CMD_SEND_STATUS = 13,
    CMD_SET_BLOCK_COUNT = 23,
    CMD_APP = 55,
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
} UsbCard;

static UsbCard cards[USB_CARD_COUNT];
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

static bool begin(void *context, uint8_t index, uint32_t address, uint32_t count)
{
    UsbCard *card = context;
    if (!settled(card, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS)) {
        return false;
    }
    write_mmc(card, MMC_CTRL, read_mmc(card, MMC_CTRL) | CTRL_FIFO_RESET);
    if (!settled(card, MMC_CTRL, CTRL_FIFO_RESET, COMMAND_TIMEOUT_MS)) {
        return false;
    }
    write_mmc(card, MMC_BLKSIZ, BLOCK_BYTES);
    write_mmc(card, MMC_BYTCNT, BLOCK_BYTES * count);
    write_mmc(card, MMC_RINTSTS, INT_ALL);
    write_mmc(card, MMC_CMDARG, address);
    write_mmc(card, MMC_CMD, CMD_BASE | CMD_DATA_READ | CMD_WRITE | index);
    card->stalled = false;
    return settled(card, MMC_CMD, CMD_START, COMMAND_TIMEOUT_MS);
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

static bool wait_ready(void *context)
{
    const UsbCard *card = context;
    uint32_t response[4] = {0, 0, 0, 0};
    SdCommand status = {CMD_SEND_STATUS, card->rca, SD_RESPONSE_SHORT, false};
    uint32_t start = hal_clock_milliseconds();
    while (hal_clock_milliseconds() - start < READY_TIMEOUT_MS) {
        if (mmc_command(card->host, status, response) &&
            (response[0] & STATUS_READY_FOR_DATA) != 0 &&
            ((response[0] >> STATUS_STATE_SHIFT) & STATUS_STATE_MASK) == STATE_TRANSFER) {
            return true;
        }
    }
    return false;
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
    return card->stream.ops != NULL && card_stream_close(&card->stream, complete);
}

const MscStream USB_CARD_STREAM = {open_stream, feed_stream, close_stream};

void usb_card_prepare(const EmmcLayout *internal)
{
    layout = internal;
    cards[USB_CARD_INTERNAL] = (UsbCard){.host = &EMMC_HOST, .rca = EMMC_RCA_ARGUMENT};
    cards[USB_CARD_SD] = (UsbCard){.host = &SD_MMC_HOST, .sd = true};
}
