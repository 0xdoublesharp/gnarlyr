#include "mmc.h"

#include "board.h"
#include "core/seam.h"
#include "cru.h"
#include "device/mmc_clock.h"
#include "emmc.h"
#include "hal/clock.h"
#include "mmc_registers.h"
#include "registers.h"
#include "sd.h"

#define CRU_MMC_DIVIDERS 0x30u
#define CRU_MMC_BUS_GATES 0x84u

enum {
    BLOCK_WORDS = 128,
    BLOCK_BYTES = 512,
    CMD_READ_SINGLE_BLOCK_INDEX = 17,
    CMD_READ_MULTIPLE_BLOCK = 18,
    CMD_STOP_TRANSMISSION = 12,
    COMMAND_TIMEOUT_MS = 100,
    BUSY_TIMEOUT_MS = 1000,
    DATA_TIMEOUT_MS = 500,
    IDENTIFY_DIVIDER_REGISTER = 1,
    IDENTIFY_CLOCK_FIELD = MMC_CRYSTAL_SOURCE | MMC_CRU_DIVIDER_MASK,
    CLOCK_UPDATE_POLLS = 50000,
    EMMC_FIELD_SHIFT = 8,
    CLOCK_LIMIT_MASK = 0x7F,
    CLOCK_LIMIT_PARKED = 0x80,
    FALLBACK_FIFO_DEPTH = 16,
    MINIMUM_FIFO_DEPTH = 8,
    MAXIMUM_FIFO_DEPTH = 1024,
    POWER_OFF_US = 10,
    POWER_ON_US = 600
};

static MmcFailure last_failure;
static uint8_t clock_limit[2];
static uint8_t pll_mhz;

static uint32_t read_register(const MmcHost *host, uint32_t offset)
{
    return register_read(host->base + offset);
}

CORE_COLD void mmc_note_failure(const MmcHost *host, MmcStep step)
{
    last_failure = (MmcFailure){step, read_register(host, MMC_RINTSTS),
                                read_register(host, MMC_STATUS), last_failure.count + 1};
}

MmcFailure mmc_last_failure(void)
{
    return last_failure;
}

static void write_register(const MmcHost *host, uint32_t offset, uint32_t value)
{
    register_write(host->base + offset, value);
}

__attribute__((noinline)) static bool wait_register(uint32_t address, uint32_t bits, bool set,
                                                    uint32_t timeout_ms)
{
    uint32_t start = hal_clock_milliseconds();
    while (((register_read(address) & bits) != 0) != set) {
        board_audio_stream_service();
        if (hal_clock_milliseconds() - start > timeout_ms) {
            return false;
        }
    }
    return true;
}

static bool wait_clear(const MmcHost *host, uint32_t offset, uint32_t bits, uint32_t timeout_ms)
{
    return wait_register(host->base + offset, bits, false, timeout_ms);
}

static bool wait_any(const MmcHost *host, uint32_t offset, uint32_t bits, uint32_t timeout_ms)
{
    return wait_register(host->base + offset, bits, true, timeout_ms);
}

CORE_COLD static bool update_clock(const MmcHost *host)
{
    write_register(host, MMC_RINTSTS, INT_HARDWARE_LOCKED);
    write_register(host, MMC_CMD, CMD_UPDATE_CLOCK);
    uint32_t polls = CLOCK_UPDATE_POLLS;
    while ((read_register(host, MMC_CMD) & CMD_START) != 0) {
        if (--polls == 0) {
            return false;
        }
        board_audio_stream_service();
    }
    return (read_register(host, MMC_RINTSTS) & INT_HARDWARE_LOCKED) == 0;
}

static uint32_t fifo_depth(const MmcHost *host)
{
    uint32_t watermark = read_register(host, MMC_FIFOTH) >> FIFOTH_RX_WATERMARK_SHIFT;
    uint32_t depth = (watermark & FIFOTH_RX_WATERMARK_MASK) + 1;
    return depth < MINIMUM_FIFO_DEPTH || depth > MAXIMUM_FIFO_DEPTH ? FALLBACK_FIFO_DEPTH : depth;
}

CORE_COLD void mmc_reset_controller(const MmcHost *host)
{
    write_register(host, MMC_CTRL, CTRL_RESET_ALL);
    wait_clear(host, MMC_CTRL, CTRL_RESET_ALL, COMMAND_TIMEOUT_MS);
    *host->fifo_depth = fifo_depth(host);
    write_register(host, MMC_FIFOTH, FIFOTH_VALUE);
    write_register(host, MMC_CTYPE, 0);
    write_register(host, MMC_CLKSRC, 0);
    write_register(host, MMC_TMOUT, TMOUT_VALUE);
    write_register(host, MMC_RINTSTS, INT_ALL);
    write_register(host, MMC_INTMASK, 0);
    write_register(host, MMC_PWREN, 1);
}

static uint32_t host_index(const MmcHost *host)
{
    return host->base == EMMC_BASE;
}

static uint32_t source_field(const MmcHost *host)
{
    uint32_t value = register_read(CRU_BASE + CRU_MMC_DIVIDERS);
    return value >> (host_index(host) * EMMC_FIELD_SHIFT) & MMC_CRU_FIELD_MASK;
}

static void set_source(const MmcHost *host, uint32_t field)
{
    cru_write(CRU_MMC_DIVIDERS, ((MMC_CRU_FIELD_MASK << 16) | field)
                                    << (host_index(host) * EMMC_FIELD_SHIFT));
}

CORE_COLD void mmc_clock_init(const MmcHost *host)
{
    set_source(host, IDENTIFY_CLOCK_FIELD);
}

__attribute__((noinline)) static bool enable_clock(const MmcHost *host, uint32_t enable)
{
    write_register(host, MMC_CLKENA, enable);
    return update_clock(host);
}

CORE_COLD static bool disable_clock(const MmcHost *host)
{
    return wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY | STATUS_DATA_BUSY, BUSY_TIMEOUT_MS) &&
           wait_clear(host, MMC_CMD, CMD_START, COMMAND_TIMEOUT_MS) && enable_clock(host, 0);
}

CORE_COLD static bool program_clock(const MmcHost *host, uint32_t field, bool identify)
{
    if (!disable_clock(host)) {
        return false;
    }
    write_register(host, MMC_CLKDIV, identify ? IDENTIFY_DIVIDER_REGISTER : 0);
    if (!update_clock(host)) {
        return false;
    }
    set_source(host, field);
    return enable_clock(host, CLKENA_ON);
}

CORE_COLD static bool apply_limit(const MmcHost *host, uint8_t *limit)
{
    if ((*limit & CLOCK_LIMIT_PARKED) != 0) {
        return true;
    }
    uint32_t field = mmc_clock_field(pll_mhz, *limit);
    if (field == MMC_CRYSTAL_SOURCE || program_clock(host, field, false)) {
        return true;
    }
    *limit |= CLOCK_LIMIT_PARKED;
    return program_clock(host, MMC_CRYSTAL_SOURCE, false);
}

CORE_COLD bool mmc_set_clock(const MmcHost *host, SdClock clock)
{
    uint8_t *limit = &clock_limit[host_index(host)];
    if (clock == SD_CLOCK_HIGH_SPEED) {
        *limit &= CLOCK_LIMIT_MASK;
        return apply_limit(host, limit) && (source_field(host) & MMC_CRYSTAL_SOURCE) == 0;
    }
    bool identify = clock == SD_CLOCK_IDENTIFY;
    *limit = identify ? 0 : *limit | CLOCK_LIMIT_PARKED;
    return program_clock(host, identify ? IDENTIFY_CLOCK_FIELD : MMC_CRYSTAL_SOURCE, identify);
}

CORE_COLD bool mmc_clock_accept(const MmcHost *host, uint8_t limit_mhz)
{
    uint8_t *limit = &clock_limit[host_index(host)];
    *limit = limit_mhz;
    return apply_limit(host, limit);
}

CORE_COLD bool mmc_stop_clock(const MmcHost *host)
{
    clock_limit[host_index(host)] = 0;
    if (!disable_clock(host)) {
        return false;
    }
    mmc_clock_init(host);
    return true;
}

static bool park_pll(const MmcHost *host)
{
    uint32_t index = host_index(host);
    uint32_t gates = register_read(CRU_BASE + CRU_MMC_BUS_GATES);
    if ((source_field(host) & MMC_CRYSTAL_SOURCE) != 0 || (gates & (1u << (8 + index))) != 0) {
        return true;
    }
    if (clock_limit[index] == 0) {
        return (read_register(host, MMC_CLKENA) & 1u) == 0 || mmc_stop_clock(host);
    }
    return program_clock(host, MMC_CRYSTAL_SOURCE, false);
}

CORE_COLD bool mmc_park_pll(void)
{
    return park_pll(&SD_MMC_HOST) && park_pll(&EMMC_HOST);
}

CORE_COLD void mmc_resume_pll(uint32_t mhz)
{
    pll_mhz = (uint8_t)mhz;
    apply_limit(&SD_MMC_HOST, &clock_limit[0]);
    apply_limit(&EMMC_HOST, &clock_limit[1]);
}

bool mmc_wait_not_busy(const MmcHost *host)
{
    return wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS);
}

CORE_COLD void mmc_set_power(const MmcHost *host, bool on)
{
    write_register(host, MMC_PWREN, on ? 1u : 0u);
}

uint32_t mmc_interrupts(const MmcHost *host)
{
    return read_register(host, MMC_RINTSTS);
}

bool mmc_powered(const MmcHost *host)
{
    return (read_register(host, MMC_PWREN) & 1u) != 0;
}

CORE_COLD void mmc_power_cycle(const MmcHost *host)
{
    write_register(host, MMC_PWREN, 0);
    board_delay_us(POWER_OFF_US);
    write_register(host, MMC_PWREN, 1);
    board_delay_us(POWER_ON_US);
}

CORE_COLD void mmc_set_bus_width(const MmcHost *host, uint8_t bits)
{
    uint32_t type = bits == 8 ? CTYPE_8_BIT : bits == 4 ? CTYPE_4_BIT : 0;
    write_register(host, MMC_CTYPE, type);
}

static uint32_t command_flags(SdCommand command)
{
    uint32_t flags = command.initialise ? CMD_INITIALISE : 0;
    if (command.response != SD_RESPONSE_NONE) {
        flags |= CMD_RESPONSE;
    }
    if (command.response == SD_RESPONSE_LONG) {
        flags |= CMD_LONG;
    }
    if (command.response != SD_RESPONSE_NONE && command.response != SD_RESPONSE_SHORT_NO_CRC) {
        flags |= CMD_CHECK_CRC;
    }
    return flags;
}

static bool command_failed(uint32_t flags, uint32_t status)
{
    uint32_t errors = INT_RESPONSE_ERROR | INT_RESPONSE_TIMEOUT | INT_HARDWARE_LOCKED;
    if ((flags & CMD_CHECK_CRC) != 0) {
        errors |= INT_RESPONSE_CRC;
    }
    return (status & errors) != 0;
}

__attribute__((noinline)) static bool start_command(const MmcHost *host, uint32_t argument,
                                                    uint32_t command)
{
    write_register(host, MMC_RINTSTS, INT_ALL);
    write_register(host, MMC_CMDARG, argument);
    write_register(host, MMC_CMD, CMD_BASE | command);
    return wait_clear(host, MMC_CMD, CMD_START, COMMAND_TIMEOUT_MS);
}

static bool response_ready(const MmcHost *host, uint32_t flags)
{
    return wait_any(host, MMC_RINTSTS, INT_COMMAND_DONE, COMMAND_TIMEOUT_MS) &&
           !command_failed(flags, read_register(host, MMC_RINTSTS));
}

static void read_response(const MmcHost *host, uint32_t response[4])
{
    for (uint32_t index = 0; index < 4; index++) {
        response[index] = read_register(host, MMC_RESP0 + 4u * index);
    }
}

__attribute__((noinline)) bool mmc_command(const MmcHost *host, SdCommand command,
                                           uint32_t response[4])
{
    if (!wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS)) {
        return false;
    }
    uint32_t flags = command_flags(command);
    if (!start_command(host, command.argument, flags | command.index) ||
        !response_ready(host, flags)) {
        return false;
    }
    read_response(host, response);
    return command.response != SD_RESPONSE_SHORT_BUSY ||
           wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS);
}

static bool prepare_data(const MmcHost *host)
{
    if (!wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS)) {
        return false;
    }
    if ((read_register(host, MMC_STATUS) & STATUS_FIFO_EMPTY) == 0) {
        write_register(host, MMC_CTRL, read_register(host, MMC_CTRL) | CTRL_FIFO_RESET);
        return wait_clear(host, MMC_CTRL, CTRL_FIFO_RESET, COMMAND_TIMEOUT_MS);
    }
    return true;
}

__attribute__((optimize("O2"))) static bool drain_fifo(const MmcHost *host, uint32_t *words,
                                                       uint32_t total)
{
    uint32_t fifo = host->base + MMC_FIFO;
    uint32_t received = 0;
    uint32_t start = hal_clock_milliseconds();
    while (received < total) {
        if ((read_register(host, MMC_RINTSTS) & INT_DATA_ABORT) != 0 ||
            hal_clock_milliseconds() - start > DATA_TIMEOUT_MS) {
            return false;
        }
        uint32_t status = read_register(host, MMC_STATUS);
        uint32_t count = (status >> STATUS_FIFO_COUNT_SHIFT) & STATUS_FIFO_COUNT_MASK;
        uint32_t left = total - received;
        uint32_t end = received + (count < left ? count : left);
        for (; received < end; received++) {
            words[received] = register_read(fifo);
        }
        board_audio_stream_service();
    }
    return true;
}

static CORE_COLD bool failed(const MmcHost *host, MmcStep step);

static bool start_data(const MmcHost *host, uint32_t command, uint32_t address, uint32_t bytes)
{
    write_register(host, MMC_BLKSIZ, bytes < BLOCK_BYTES ? bytes : BLOCK_BYTES);
    write_register(host, MMC_BYTCNT, bytes);
    return start_command(host, address, CMD_DATA_READ | command);
}

__attribute__((noinline)) static bool finish_data(const MmcHost *host)
{
    if (!wait_any(host, MMC_RINTSTS, INT_DATA_FINISHED, DATA_TIMEOUT_MS)) {
        return failed(host, MMC_STEP_DATA_WAIT);
    }
    if ((read_register(host, MMC_RINTSTS) & INT_DATA_FAILED) != 0) {
        return failed(host, MMC_STEP_DATA_ERROR);
    }
    write_register(host, MMC_RINTSTS, INT_ALL);
    return true;
}

static bool read_data(const MmcHost *host, uint8_t index, uint32_t address, uint32_t bytes,
                      uint32_t *words)
{
    if (!prepare_data(host)) {
        return failed(host, MMC_STEP_BUSY_BEFORE);
    }
    if (!start_data(host, index, address, bytes)) {
        return failed(host, MMC_STEP_COMMAND_START);
    }
    if (!drain_fifo(host, words, bytes / 4)) {
        return failed(host, MMC_STEP_FIFO);
    }
    return finish_data(host);
}

bool mmc_read_block(const MmcHost *host, uint8_t index, uint32_t address, uint32_t *words)
{
    return read_data(host, index, address, BLOCK_BYTES, words);
}

CORE_COLD bool mmc_read_sd_switch(const MmcHost *host, uint32_t argument, uint32_t words[16])
{
    return read_data(host, 6, argument, 64, words) &&
           mmc_card_status_ok(read_register(host, MMC_RESP0));
}

bool mmc_read_sd_status(const MmcHost *host, uint32_t words[16])
{
    return read_data(host, 13, 0, 64, words);
}

static bool stop_transmission(const MmcHost *host, uint32_t argument)
{
    uint32_t flags = CMD_STOP_ABORT | CMD_RESPONSE | CMD_CHECK_CRC | CMD_STOP_TRANSMISSION;
    if (!start_command(host, argument, flags) || !response_ready(host, flags)) {
        return failed(host, MMC_STEP_STOP);
    }
    if (!wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS)) {
        return failed(host, MMC_STEP_BUSY_AFTER);
    }
    write_register(host, MMC_RINTSTS, INT_ALL);
    return true;
}

bool mmc_read_blocks(const MmcHost *host, uint32_t address, uint32_t count, uint32_t stop_argument,
                     uint32_t *words)
{
    if (count == 1) {
        return read_data(host, CMD_READ_SINGLE_BLOCK_INDEX, address, BLOCK_BYTES, words);
    }
    bool read = read_data(host, CMD_READ_MULTIPLE_BLOCK, address, BLOCK_BYTES * count, words);
    return stop_transmission(host, stop_argument) && read;
}

static bool fill_fifo(const MmcHost *host, const uint32_t *words, uint32_t total)
{
    uint32_t fifo = host->base + MMC_FIFO;
    uint32_t sent = 0;
    uint32_t start = hal_clock_milliseconds();
    while (sent < total) {
        if ((read_register(host, MMC_RINTSTS) & INT_DATA_ABORT) != 0 ||
            hal_clock_milliseconds() - start > DATA_TIMEOUT_MS) {
            return false;
        }
        uint32_t status = read_register(host, MMC_STATUS);
        uint32_t count = (status >> STATUS_FIFO_COUNT_SHIFT) & STATUS_FIFO_COUNT_MASK;
        uint32_t depth = *host->fifo_depth;
        uint32_t space = count < depth ? depth - count : 0;
        if (space > 0) {
            start = hal_clock_milliseconds();
        }
        uint32_t left = total - sent;
        uint32_t end = sent + (space < left ? space : left);
        for (; sent < end; sent++) {
            register_write(fifo, words[sent]);
        }
        board_audio_stream_service();
    }
    return true;
}

static bool failed(const MmcHost *host, MmcStep step)
{
    mmc_note_failure(host, step);
    write_register(host, MMC_RINTSTS, INT_ALL);
    return false;
}

bool mmc_stop(const MmcHost *host, uint32_t argument)
{
    return stop_transmission(host, argument);
}

bool mmc_write_blocks(const MmcHost *host, uint8_t index, uint32_t address, uint32_t count,
                      const uint32_t *words)
{
    if (!prepare_data(host)) {
        return failed(host, MMC_STEP_BUSY_BEFORE);
    }
    write_register(host, MMC_BLKSIZ, BLOCK_BYTES);
    write_register(host, MMC_BYTCNT, BLOCK_BYTES * count);
    if (!start_command(host, address, CMD_DATA_READ | CMD_WRITE | index)) {
        return failed(host, MMC_STEP_COMMAND_START);
    }
    if (!fill_fifo(host, words, BLOCK_WORDS * count)) {
        return failed(host, MMC_STEP_FIFO);
    }
    if (!finish_data(host)) {
        return false;
    }
    if (!wait_clear(host, MMC_STATUS, STATUS_CARD_BUSY, BUSY_TIMEOUT_MS)) {
        return failed(host, MMC_STEP_BUSY_AFTER);
    }
    return true;
}
