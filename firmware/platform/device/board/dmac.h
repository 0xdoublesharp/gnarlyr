#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/dma_loop.h"

bool dmac_present(void);
bool dmac_start_loop(const DmaItem *item);
void dmac_stop(void);
bool dmac_running(void);
bool dmac_suspended(void);
uint32_t dmac_source(void);
void dmac_suspend(void);
void dmac_resume(void);
void dmac_push_start(uint32_t source, uint32_t destination, uint32_t words);
bool dmac_push_busy(void);
void dmac_push_wait_event(void);
void dmac_push_cancel(void);
