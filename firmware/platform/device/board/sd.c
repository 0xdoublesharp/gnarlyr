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
    CARD_OFF_MS = 20,
    POWER_UP_MS = 20,
    CMD_GO_IDLE = 0,
    RESET_PULSE_US = 10,
    DETECT_SETTLE_MS = 1
};

static const GpioPin CARD_DETECT = {0, 11};
static uint32_t sd_fifo_depth;
const MmcHost SD_MMC_HOST = {SD_BASE, CRU_SD_IDENTIFY, CRU_SD_TRANSFER, &sd_fifo_depth};

static SdCard card;
static bool ready;
static CardStart attempts;
static CardStartLog start_log;
static bool controller_on;
static bool detect_started;
static CardPresence presence;
static uint32_t generation;

static void log_failure(uint8_t index)
{
    uint32_t interrupts = mmc_interrupts(&SD_MMC_HOST);
    start_log.failures++;
    start_log.failed_command = index;
    start_log.failed_interrupts = interrupts;
    start_log.timeouts += (interrupts & MMC_INT_RESPONSE_TIMEOUT) != 0 ? 1u : 0u;
}

static bool host_command(void *context, SdCommand command, uint32_t response[4])
{
    (void)context;
    bool done = mmc_command(&SD_MMC_HOST, command, response);
    start_log.last_command = command.index;
    start_log.last_response = done ? response[0] : 0;
    if (!done) {
        log_failure(command.index);
    }
    return done;
}

static void host_set_clock(void *context, SdClock clock)
{
    (void)context;
    mmc_set_clock(&SD_MMC_HOST, clock);
}

static void host_set_bus_width(void *context, uint8_t bits)
{
    (void)context;
    mmc_set_bus_width(&SD_MMC_HOST, bits);
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
    mmc_stop_clock(&SD_MMC_HOST);
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

void sd_reset_controller(void)
{
    cru_reset(RESET_SD, true);
    board_delay_us(RESET_PULSE_US);
    cru_reset(RESET_SD, false);
}

static void clock_controller(void)
{
    cru_gate(GATE_SD_BUS, true);
    cru_gate(GATE_SD_CARD_CLOCK, true);
    cru_reset(RESET_SD, false);
    cru_write(CRU_MMC_DIVIDERS, CRU_SD_IDENTIFY);
    register_write(GRF_BASE + GRF_SD_MUX_LOW, GRF_SD_MUX_LOW_VALUE);
    register_write(GRF_BASE + GRF_SD_MUX_HIGH, GRF_SD_MUX_HIGH_VALUE);
    board_delay_ms(POWER_SETTLE_MS);
    controller_on = true;
}

static void power_card_off(void)
{
    mmc_stop_clock(&SD_MMC_HOST);
    mmc_set_power(&SD_MMC_HOST, false);
}

static void power_up_card(void)
{
    clock_controller();
    start_log.powered_before = mmc_powered(&SD_MMC_HOST);
    power_card_off();
    board_delay_ms(CARD_OFF_MS);
    mmc_reset_controller(&SD_MMC_HOST);
    board_delay_ms(POWER_UP_MS);
    mmc_set_clock(&SD_MMC_HOST, SD_CLOCK_IDENTIFY);
    board_delay_ms(POWER_SETTLE_MS);
}

void sd_quiesce(void)
{
    if (!controller_on) {
        return;
    }
    clock_controller();
    if (ready) {
        uint32_t none[4] = {0, 0, 0, 0};
        mmc_command(&SD_MMC_HOST, (SdCommand){CMD_GO_IDLE, 0, SD_RESPONSE_NONE, false}, none);
    }
    power_card_off();
    ready = false;
}

bool sd_read(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    (void)context;
    if (!sd_present() || presence.ejected || !ready || count == 0 || lba >= card.sector_count ||
        count > card.sector_count - lba) {
        return false;
    }
    return mmc_read_blocks(&SD_MMC_HOST, sd_card_address(&card, lba), count,
                           sd_card_rca_argument(&card), buffer);
}

static bool start_card(void)
{
    start_card_detect();
    start_log.detected = host_card_present(NULL);
    if (!start_log.detected) {
        return false;
    }
    power_up_card();
    uint32_t started_ms = hal_clock_milliseconds();
    start_log.attempts++;
    start_log.ready = sd_card_init(&card, &HOST);
    start_log.init_ms = hal_clock_milliseconds() - started_ms;
    return start_log.ready;
}

bool sd_start(BlockDevice *device)
{
    *device = (BlockDevice){sd_read, NULL, sd_write};
    sd_present();
    if (!presence.present || presence.ejected) {
        return false;
    }
    if (!ready && card_start_due(&attempts, hal_clock_milliseconds())) {
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
        if (mmc_command(&SD_MMC_HOST, status, words) && (words[0] & STATUS_READY_FOR_DATA) != 0 &&
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
    return mmc_write_blocks(&SD_MMC_HOST, command, address, count, words);
}

static bool stop_writing(void *context)
{
    (void)context;
    return sd_present() && mmc_stop(&SD_MMC_HOST, sd_card_rca_argument(&card));
}

static bool wait_until_ready(void *context)
{
    (void)context;
    if (wait_for_transfer_state()) {
        return true;
    }
    mmc_note_failure(&SD_MMC_HOST, MMC_STEP_TRANSFER_STATE);
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
        attempts = (CardStart){0};
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
    attempts = (CardStart){0};
}

const SdCard *sd_card_info(void)
{
    return &card;
}

bool sd_card_ready(void)
{
    return ready;
}

CardStartLog sd_start_log(void)
{
    CardStartLog log = start_log;
    log.raw_now = detect_started && host_card_present(NULL);
    log.present_now = presence.present;
    log.ejected_now = presence.ejected;
    return log;
}
