#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "cru.h"
#include "device/audio_math.h"
#include "gpio.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "registers.h"

#define I2C1_BASE 0x40050000u
#define I2C_CON (I2C1_BASE + 0x00u)
#define I2C_CLKDIV (I2C1_BASE + 0x04u)
#define I2C_MRXADDR (I2C1_BASE + 0x08u)
#define I2C_MRXRADDR (I2C1_BASE + 0x0Cu)
#define I2C_MTXCNT (I2C1_BASE + 0x10u)
#define I2C_MRXCNT (I2C1_BASE + 0x14u)
#define I2C_IEN (I2C1_BASE + 0x18u)
#define I2C_IPD (I2C1_BASE + 0x1Cu)
#define I2C_TXDATA (I2C1_BASE + 0x100u)
#define I2C_RXDATA (I2C1_BASE + 0x200u)
#define I2C_MRXADDR_VALID 0x01000000u
#define GRF_PIN_GROUPS (GRF_BASE + 0xBCu)
#define GRF_I2C1_SHIFT 2u
#define GRF_I2C1_FIELD 3u
#define GRF_I2C1_GROUP_C 2u
#define GRF_WRITE_ENABLE_SHIFT 16u
#define CRU_GATE_BASE 0x80u
#define CRU_RESET_BASE 0xC0u

enum {
    GATE_I2C1 = 0x84,
    RESET_I2C1 = 0x1B,
    CRU_BITS_PER_REGISTER = 16,
    PIN_FUNCTION_I2C1 = 3,
    RTC_ADDRESS = 0xA2,
    RTC_FIRST_REGISTER = 0x00,
    I2C_ENABLE = 0x01,
    I2C_MODE_RECEIVE = 0x02,
    I2C_START = 0x08,
    I2C_STOP = 0x10,
    I2C_NAK_LAST = 0x20,
    I2C_ALL_PENDING = 0x7F,
    I2C_TX_DONE = 0x04,
    I2C_RX_DONE = 0x08,
    I2C_START_DONE = 0x10,
    I2C_STOP_DONE = 0x20,
    I2C_NAK = 0x40,
    I2C_TIMEOUT_MS = 10,
    I2C_BUS_KHZ = 100,
    RESET_PULSE_US = 200,
    BYTES_PER_WORD = 4
};

static const GpioPin RTC_PINS[] = {{2, 12}, {2, 13}};

typedef struct BusState {
    uint32_t functions[2];
    uint32_t group;
    bool gated;
    bool reset_held;
} BusState;

static bool cru_bit(uint32_t base, uint32_t id)
{
    uint32_t address = CRU_BASE + base + 4u * (id / CRU_BITS_PER_REGISTER);
    return (register_read(address) >> (id % CRU_BITS_PER_REGISTER) & 1u) != 0;
}

static BusState save_bus(void)
{
    return (BusState){
        .functions = {gpio_function(RTC_PINS[0]), gpio_function(RTC_PINS[1])},
        .group = register_read(GRF_PIN_GROUPS) >> GRF_I2C1_SHIFT & GRF_I2C1_FIELD,
        .gated = cru_bit(CRU_GATE_BASE, GATE_I2C1),
        .reset_held = cru_bit(CRU_RESET_BASE, RESET_I2C1),
    };
}

static void select_group(uint32_t group)
{
    uint32_t mask = GRF_I2C1_FIELD << GRF_I2C1_SHIFT;
    register_write(GRF_PIN_GROUPS, mask << GRF_WRITE_ENABLE_SHIFT | group << GRF_I2C1_SHIFT);
}

static void open_bus(void)
{
    cru_gate(GATE_I2C1, true);
    gpio_mux(RTC_PINS[0], PIN_FUNCTION_I2C1);
    gpio_mux(RTC_PINS[1], PIN_FUNCTION_I2C1);
    select_group(GRF_I2C1_GROUP_C);
    cru_reset(RESET_I2C1, true);
    board_delay_us(RESET_PULSE_US);
    cru_reset(RESET_I2C1, false);
    register_write(I2C_CON, 0);
    register_write(I2C_CLKDIV, i2c_clock_divider(board_pclk_hz(), I2C_BUS_KHZ));
}

static void restore_bus(const BusState *state)
{
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_CON, 0);
    cru_reset(RESET_I2C1, state->reset_held);
    select_group(state->group);
    gpio_mux(RTC_PINS[0], state->functions[0]);
    gpio_mux(RTC_PINS[1], state->functions[1]);
    cru_gate(GATE_I2C1, !state->gated);
}

static HalRtcStatus wait_pending(uint32_t bits)
{
    uint32_t start = hal_clock_milliseconds();
    while ((register_read(I2C_IPD) & bits) == 0) {
        if (hal_clock_milliseconds() - start >= I2C_TIMEOUT_MS) {
            return HAL_RTC_TIMEOUT;
        }
    }
    return HAL_RTC_OK;
}

static HalRtcStatus bus_start(uint32_t mode)
{
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_CON, I2C_ENABLE | mode | I2C_START);
    HalRtcStatus status = wait_pending(I2C_START_DONE);
    register_write(I2C_IPD, I2C_START_DONE);
    register_write(I2C_CON, I2C_ENABLE | mode);
    return status;
}

static HalRtcStatus bus_stop(HalRtcStatus status)
{
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_CON, I2C_ENABLE | I2C_STOP);
    HalRtcStatus stopped = wait_pending(I2C_STOP_DONE);
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_CON, 0);
    return status != HAL_RTC_OK ? status : stopped;
}

static HalRtcStatus point_at_first_register(void)
{
    HalRtcStatus status = bus_start(0);
    if (status != HAL_RTC_OK) {
        return bus_stop(status);
    }
    register_write(I2C_TXDATA, RTC_ADDRESS | RTC_FIRST_REGISTER << 8);
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_MTXCNT, 2);
    status = wait_pending(I2C_TX_DONE | I2C_NAK);
    if (status == HAL_RTC_OK && (register_read(I2C_IPD) & I2C_NAK) != 0) {
        status = HAL_RTC_NO_ACK;
    }
    return bus_stop(status);
}

static void take_bytes(uint8_t *registers)
{
    for (uint32_t word = 0; word < HAL_RTC_REGISTERS / BYTES_PER_WORD; word++) {
        uint32_t value = register_read(I2C_RXDATA + BYTES_PER_WORD * word);
        for (uint32_t byte = 0; byte < BYTES_PER_WORD; byte++) {
            registers[word * BYTES_PER_WORD + byte] = (uint8_t)(value >> (8 * byte));
        }
    }
}

static HalRtcStatus receive_registers(uint8_t *registers)
{
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_IEN, 0);
    register_write(I2C_MRXADDR, I2C_MRXADDR_VALID | (RTC_ADDRESS + 1u));
    register_write(I2C_MRXRADDR, 0);
    HalRtcStatus status = bus_start(I2C_MODE_RECEIVE);
    if (status != HAL_RTC_OK) {
        return bus_stop(status);
    }
    register_write(I2C_IPD, I2C_ALL_PENDING);
    register_write(I2C_CON, register_read(I2C_CON) | I2C_NAK_LAST);
    register_write(I2C_MRXCNT, HAL_RTC_REGISTERS);
    status = wait_pending(I2C_RX_DONE | I2C_NAK);
    if (status == HAL_RTC_OK && (register_read(I2C_IPD) & I2C_NAK) != 0) {
        status = HAL_RTC_NO_ACK;
    }
    if (status == HAL_RTC_OK) {
        take_bytes(registers);
    }
    return bus_stop(status);
}

HalRtcRead hal_system_rtc_read(void)
{
    HalRtcRead read = {.status = HAL_RTC_OK};
    BusState state = save_bus();
    open_bus();
    read.status = point_at_first_register();
    if (read.status == HAL_RTC_OK) {
        read.status = receive_registers(read.registers);
    }
    restore_bus(&state);
    hal_system_keep_alive();
    return read;
}
