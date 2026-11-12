#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/dac_identity.h"

typedef enum DacPin {
    DAC_PIN_MUTE,
    DAC_PIN_ENABLE_A,
    DAC_PIN_ENABLE_B,
    DAC_PIN_RESET,
    DAC_PIN_AMP
} DacPin;

typedef enum DacResult {
    DAC_READY,
    DAC_NO_REPLY,
    DAC_NOT_READY
} DacResult;

typedef struct DacBus {
    bool (*write)(void *context, uint8_t address, uint32_t map, uint8_t value);
    bool (*read)(void *context, uint8_t address, uint32_t map, uint8_t *value);
    void (*set_pin)(void *context, DacPin pin, bool level);
    void (*delay_us)(void *context, uint32_t microseconds);
    void *context;
} DacBus;

typedef enum DacCurve {
    DAC_CURVE_SINGLE_ENDED,
    DAC_CURVE_BALANCED
} DacCurve;

typedef struct DacAnalog {
    bool gain_high;
    uint8_t filter;
} DacAnalog;

enum {
    DAC_ANALOG_FILTERS = 5,
    DAC_LEFT_ADDRESS = 0x60,
    DAC_RIGHT_ADDRESS = 0x66,
    DAC_VOLUME_LEVELS = 60,
    DAC_VOLUME_MUTED = 0xFF
};

void dac_power_pins(const DacBus *bus);
void dac_read_identity(const DacBus *bus, DacIdentity *chips);
bool dac_probe(const DacBus *bus);
DacResult dac_configure(const DacBus *bus, uint8_t rate_byte, DacAnalog analog);
bool dac_apply_analog(const DacBus *bus, DacAnalog before, DacAnalog after);
void dac_output_on(const DacBus *bus, uint8_t volume);
void dac_output_off(const DacBus *bus);
void dac_set_muted(const DacBus *bus, bool muted, uint8_t volume);
void dac_set_volume(const DacBus *bus, uint8_t volume);
uint8_t dac_volume_value(uint8_t level, DacCurve curve);
uint8_t dac_ramp_next(uint8_t current, uint8_t target);

bool dac_sequence_power_down(const DacBus *bus);
bool dac_sequence_power_up(const DacBus *bus);
bool dac_sequence_gain(const DacBus *bus, bool high);

typedef struct DacDiagnostics {
    uint32_t start_us;
    uint32_t stop_us;
    uint32_t gain_us;
    uint8_t power[2];
    uint8_t valid;
    bool up;
} DacDiagnostics;
