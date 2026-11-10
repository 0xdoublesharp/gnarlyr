#pragma once

#include "device/audio_resample.h"

bool board_audio_dsp_prepare(void);
bool board_audio_dsp_pull(AudioResample *resample, int32_t *left, int32_t *right);
