#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/dac_sequence.h"
#include "hal/audio.h"

HalToneResult dac_path_start(uint32_t sample_rate);
void dac_path_stop(void);
void dac_path_set_fade_service(void (*service)(void));
void dac_path_output_on(void);
void dac_path_set_muted(bool mute);
void dac_path_stream_quiet(void);
void dac_path_fade_out(void);
void dac_path_set_volume(uint8_t value);
void dac_path_set_gain(bool high);
void dac_path_set_filter(uint8_t filter);
bool dac_path_take_underrun(void);
bool dac_path_is_muted(void);
bool dac_path_read_register(uint32_t map, uint8_t *left, uint8_t *right);
void dac_path_start_transfer(void);
void dac_path_stop_transfer(void);

void dac_path_guard(bool held);

DacDiagnostics dac_path_diagnostics(void);
