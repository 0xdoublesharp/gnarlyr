#pragma once

#include "device/audio_resample.h"
#include "device/core1_stream.h"

bool board_audio_dsp_prepare(void);
uint32_t board_audio_dsp_render(AudioResample *resample, uint32_t *words, uint32_t frames);

bool board_audio_ogg_prepare(void);

bool board_audio_vorbis_wait(Core1Source *source);
