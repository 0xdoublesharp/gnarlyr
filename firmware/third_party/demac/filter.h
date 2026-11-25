/*

libdemac - A Monkey's Audio decoder


Copyright (C) Dave Chapman 2007

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110, USA

*/

#ifndef _APE_FILTER_H
#define _APE_FILTER_H

#include "demac_config.h"
#include <stdbool.h>

typedef struct DemacFilterJob {
    int version;
    int level;
    int count;
    bool frame_start;
    int32_t *samples[2];
} DemacFilterJob;

typedef bool (*DemacFilterHook)(void *context, DemacFilterJob job);

typedef struct DemacFilterHistory {
    const int32_t *residuals;
    const int32_t *filtered;
    int count;
} DemacFilterHistory;

void observe_filter_16_11(int version, int channel, DemacFilterHistory history);
void observe_filter_64_11(int version, int channel, DemacFilterHistory history);
void observe_filter_256_13(int version, int channel, DemacFilterHistory history);

void demac_filter_init(int level);
void demac_filter_apply(DemacFilterJob job);
void demac_filter_observe(DemacFilterJob job, const int32_t *const filtered[2]);
void demac_filter_set_hook(DemacFilterHook hook, void *context);
void demac_filter_decode(DemacFilterJob job);

void init_filter_16_11(filter_int* buf);
void apply_filter_16_11(int fileversion, int channel,
                        int32_t* decoded, int count);

void init_filter_64_11(filter_int* buf);
void apply_filter_64_11(int fileversion, int channel,
                        int32_t* decoded, int count);

void init_filter_32_10(filter_int* buf);
void apply_filter_32_10(int fileversion, int channel,
                        int32_t* decoded, int count);

void init_filter_256_13(filter_int* buf);
void apply_filter_256_13(int fileversion, int channel,
                         int32_t* decoded, int count);

void init_filter_1280_15(filter_int* buf);
void apply_filter_1280_15(int fileversion, int channel,
                          int32_t* decoded, int count);

#endif
