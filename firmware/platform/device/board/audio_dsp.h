#pragma once

#include "device/audio_resample.h"
#include "device/core1_stream.h"

bool board_audio_dsp_prepare(void);
bool board_audio_dsp_pull(AudioResample *resample, int32_t *left, int32_t *right);

bool board_audio_vorbis_prepare(void);

bool board_audio_vorbis_wait(Core1Source *source);
