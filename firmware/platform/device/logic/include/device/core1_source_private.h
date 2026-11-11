#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/core1_stream.h"

void core1_publish(void);
void core1_observe(void);
uint32_t core1_smaller(uint32_t a, uint32_t b);
void core1_source_stop(Core1Source *source);
uint32_t core1_source_next_generation(Core1Source *source);
bool core1_open_flac(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
bool core1_open_mp3(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);
bool core1_open_m4a(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);

bool core1_open_ogg(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track);

bool core1_ogg_read(void *context, uint32_t offset, void *buffer, uint32_t length);
