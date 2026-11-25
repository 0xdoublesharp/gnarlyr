#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/dac_eq.h"
#include "hal/audio.h"

HalToneResult dac_path_start(uint32_t sample_rate, bool dop);
void dac_path_stop(void);
void dac_path_set_fade_service(void (*service)(void));
void dac_path_output_on(void);
void dac_path_set_muted(bool mute);
void dac_path_stream_quiet(void);
void dac_path_fade_out(void);
void dac_path_set_volume(uint8_t value, uint8_t left, uint8_t right);
uint8_t dac_overflow_flags(void);
void dac_path_service(void);
bool dac_path_set_gain(HalAudioGain gain);
bool dac_path_set_filter(uint8_t filter);
bool dac_path_set_equalizer(const DacEqProgram *program);
bool dac_path_take_underrun(void);
bool dac_path_is_muted(void);
bool dac_path_is_guarded(void);
bool dac_path_is_up(void);
bool dac_read(void *context, uint8_t address, uint32_t map, uint8_t *value);
void dac_path_start_transfer(void);
void dac_path_stop_transfer(void);

void dac_path_guard(bool held);

DacDiagnostics dac_path_diagnostics(void);
