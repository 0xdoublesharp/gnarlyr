#pragma once

#include <stdbool.h>
#include <stdint.h>

const char *hal_system_version(void);
uint32_t hal_system_clock_mhz(void);
bool hal_system_set_clock_mhz(uint32_t mhz);
void hal_system_coast(bool coast);

typedef struct HalBootClocks {
    uint32_t mode;
    uint32_t clock_select;
    uint32_t pclk_select;
    uint32_t pll_con0;
    uint32_t pll_con1;
    uint32_t core_hz;
} HalBootClocks;

HalBootClocks hal_system_boot_clocks(void);

typedef struct HalUsbCore {
    bool captured;
    uint32_t hwcfg2;
    uint32_t hwcfg3;
    uint32_t hwcfg4;
    uint32_t usbcfg;
    uint32_t rx_fifo;
    uint32_t nptx_fifo;
} HalUsbCore;

HalUsbCore hal_system_usb_core(void);

typedef enum HalUsbEnd {
    HAL_USB_END_NONE,
    HAL_USB_END_EJECTED,
    HAL_USB_END_UNPLUGGED,
    HAL_USB_END_NO_HOST
} HalUsbEnd;

enum {
    HAL_USB_SETUP_TRACE = 8
};

typedef enum HalUsbSetupOutcome {
    HAL_USB_SETUP_NONE,
    HAL_USB_SETUP_ANSWERED,
    HAL_USB_SETUP_STALLED,
    HAL_USB_SETUP_RECEIVING
} HalUsbSetupOutcome;

typedef struct HalUsbSetup {
    uint8_t request_type;
    uint8_t request;
    uint8_t outcome;
    uint16_t value;
    uint16_t length;
} HalUsbSetup;

typedef struct HalUsbSetupTrace {
    HalUsbSetup setups[HAL_USB_SETUP_TRACE];
    uint8_t seen;
} HalUsbSetupTrace;

typedef struct HalUsbWrites {
    uint32_t commands;
    uint32_t receive_ms;
    uint32_t card_ms;
    uint32_t idle_ms;
    uint8_t fast_cards;
    uint8_t fallbacks;
    uint16_t fifo_words[2];
    uint32_t ui_ms;
    uint32_t hcon[2];
} HalUsbWrites;

typedef struct HalUsbLast {
    HalUsbEnd end;
    uint8_t left_mounted;
    bool internal_write_ok;
    uint8_t speed_fallback;
    uint8_t fallback_commands;
    uint32_t internal_written;
    uint32_t card_written;
    uint32_t writes_after_eject;
    HalUsbSetupTrace fallback_setups;
    HalUsbWrites writes;
} HalUsbLast;

HalUsbLast hal_system_usb_last(void);

typedef enum HalUsbMode {
    HAL_USB_MODE_STORAGE,
    HAL_USB_MODE_DAC,
    HAL_USB_MODE_PLAYER
} HalUsbMode;

typedef enum HalUsbAnswer {
    HAL_USB_ANSWER_ASK,
    HAL_USB_ANSWER_STORAGE,
    HAL_USB_ANSWER_CHARGE,
    HAL_USB_ANSWER_DAC
} HalUsbAnswer;

void hal_system_set_usb_mode(HalUsbMode mode);
void hal_system_set_usb_answer(HalUsbAnswer answer);
void hal_system_request_usb_mode(HalUsbMode mode);
void hal_system_set_dac_volume(uint8_t level);

typedef struct HalLoopCycles {
    uint32_t register_centi;
    uint32_t memory_centi;
} HalLoopCycles;

HalLoopCycles hal_system_loop_cycles(void);
typedef enum HalProfileState {
    HAL_PROFILE_OFF,
    HAL_PROFILE_ON,
    HAL_PROFILE_FULL
} HalProfileState;

HalProfileState hal_system_profile_state(void);
void hal_system_set_profiling(bool on);
void hal_system_reset_profile(void);
typedef struct HalUsbSense {
    bool kept_on;
    bool cable;
    bool powered;
    bool session;
    bool connecting;
} HalUsbSense;

HalUsbSense hal_system_usb_sense(void);
void hal_system_keep_usb_on(bool kept_on);
void hal_system_usb_reconnect(void);
bool hal_system_usb_connect_pending(void);
void hal_system_usb_connect(bool on);
typedef enum HalProbeKind {
    HAL_PROBE_PEEK,
    HAL_PROBE_POKE,
    HAL_PROBE_PIN,
    HAL_PROBE_DAC
} HalProbeKind;

enum {
    HAL_PROBE_CABLE = 1,
    HAL_PROBE_CHARGE_KNOWN = 2,
    HAL_PROBE_CHARGE_PIN = 4,
    HAL_PROBE_SUPPLY_OFF = 8
};

typedef struct HalProbe {
    bool allowed;
    uint8_t levels;
    uint32_t before;
    uint32_t after;
} HalProbe;

HalProbe hal_system_probe(HalProbeKind kind, uint32_t first, uint32_t second);

enum {
    HAL_RTC_REGISTERS = 16
};

typedef enum HalRtcStatus {
    HAL_RTC_OK,
    HAL_RTC_NO_ACK,
    HAL_RTC_TIMEOUT,
    HAL_RTC_NO_TIME,
    HAL_RTC_MISMATCH,
    HAL_RTC_STATUS_COUNT
} HalRtcStatus;

typedef struct HalRtcRead {
    HalRtcStatus status;
    uint8_t registers[HAL_RTC_REGISTERS];
} HalRtcRead;

HalRtcRead hal_system_rtc_read(void);
HalRtcRead hal_system_rtc_set(uint32_t utc_s);
uint8_t hal_system_power_levels(void);
uint32_t hal_system_power_samples(void);
void hal_system_hold_usb_supply_off(bool held);
void hal_system_save_report(uint32_t reason);
void hal_system_script_mark(uint32_t line);
void hal_system_stall_test(void);
void hal_system_watchdog_test(void);
void hal_system_keep_alive(void);
uint32_t hal_system_idle_microseconds(void);

typedef struct HalCore1Load {
    uint32_t elapsed_cycles;
    uint32_t idle_cycles;
} HalCore1Load;

HalCore1Load hal_system_core1_load(void);
typedef enum HalHighRamStatus {
    HAL_HIGH_RAM_OK,
    HAL_HIGH_RAM_UNAVAILABLE,
    HAL_HIGH_RAM_BAD_MEMORY,
    HAL_HIGH_RAM_BAD_CALL
} HalHighRamStatus;

typedef struct HalHighRamTest {
    HalHighRamStatus status;
    uint32_t size;
    uint32_t bad_address;
    uint32_t call_result;
} HalHighRamTest;

HalHighRamTest hal_system_high_ram_test(void);
bool hal_system_restart_pending(void);
bool hal_system_settle_user_state(bool requested);
const void *hal_system_load_rescan_code(void);
const void *hal_system_load_module(uint32_t module);
const void *hal_system_loaded_module(void);
const void *hal_system_load_debug_code(void);
const void *hal_system_extension_code(void);
void *hal_system_borrow_scratch(uint32_t *size);
void *hal_system_borrow_listing(uint32_t *size);
void hal_system_return_scratch(void);

typedef enum HalStartStep {
    HAL_START_STEP_NONE,
    HAL_START_STEP_BACKLIGHT_ON,
    HAL_START_STEP_BACKLIGHT_OFF,
    HAL_START_STEP_CLOCK_SWITCH,
    HAL_START_STEP_CORE1_START,
    HAL_START_STEP_CORE1_STOP,
    HAL_START_STEP_CARD_WRITE,
    HAL_START_STEP_WATCHDOG_TEST,
    HAL_START_STEP_LOADER,
    HAL_START_STEP_COUNT
} HalStartStep;

typedef struct HalStartInfo {
    bool warm;
    uint32_t starts;
    uint32_t previous_uptime_ms;
    bool previous_power_off;
    uint32_t fault_status;
    uint32_t hard_fault_status;
    uint32_t fault_pc;
    HalStartStep last_step;
    bool last_step_finished;
    uint32_t last_step_ms;
    uint32_t last_millivolts;
} HalStartInfo;

HalStartInfo hal_system_start_info(void);

typedef enum HalCore1Status {
    HAL_CORE1_RAN,
    HAL_CORE1_UNAVAILABLE,
    HAL_CORE1_NEEDS_PLL,
    HAL_CORE1_NO_POWER,
    HAL_CORE1_NO_REPLY,
    HAL_CORE1_BAD_REPLY,
    HAL_CORE1_NO_STOP,
    HAL_CORE1_BUSY
} HalCore1Status;

typedef struct HalCore1Test {
    HalCore1Status status;
    uint32_t mhz;
    HalLoopCycles cycles;
} HalCore1Test;

HalCore1Test hal_system_core1_test(void);

typedef struct HalCore1Accel {
    HalCore1Status status;
    uint32_t mhz;
    bool narrow_ok;
    bool wide_ok;
    bool timed_out;
    uint8_t failed_vector;
    uint32_t failed_sample;
    uint32_t hardware_us;
    uint32_t software_us;
} HalCore1Accel;

HalCore1Accel hal_system_core1_accel_test(void);
