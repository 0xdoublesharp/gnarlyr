#include <stdint.h>

#include "core1_region.h"
#include "device/core1_control.h"
#include "device/dsd_stream.h"
#include "idle.h"
#include "registers.h"

void core1_main(void);

static Core1Dsd dsd;
static Core1Stream stream;

void core1_main(void)
{
    register_write(CORE1_SHARED_READY, CORE1_READY_MAGIC);
    __asm__ volatile("dsb" ::: "memory");
    register_write(CORE1_MAILBOX_B2A_COMMAND, CORE1_READY_COMMAND);
    register_write(CORE1_MAILBOX_B2A_DATA, 0);
    if (register_read(CORE1_SHARED_COMMAND) != CORE1_COMMAND_STREAM) {
        return;
    }
    StreamRegion region = core1_region();
    region.request_clock = core1_request_clock;
    region.ring_bytes = DSD_RING_BYTES;
    core1_stream_init(&stream, region, core1_dsd_decoder(&dsd, region));
    core1_idle_start();
    for (;;) {
        if (!core1_stream_step(&stream)) {
            core1_idle();
        }
    }
}
