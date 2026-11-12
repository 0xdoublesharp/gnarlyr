#include <stdint.h>

#include "core1_region.h"
#include "device/core1_control.h"
#include "device/core1_vorbis.h"
#include "idle.h"
#include "registers.h"

void core1_main(void);

static Core1Vorbis vorbis;
static Core1Stream stream;

int abs(int value);
long labs(long value);

int abs(int value)
{
    return value < 0 ? -value : value;
}

long labs(long value)
{
    return value < 0 ? -value : value;
}

void core1_main(void)
{
    register_write(CORE1_SHARED_READY, CORE1_READY_MAGIC);
    __asm__ volatile("dsb" ::: "memory");
    register_write(CORE1_MAILBOX_B2A_COMMAND, CORE1_READY_COMMAND);
    register_write(CORE1_MAILBOX_B2A_DATA, 0);
    if (register_read(CORE1_SHARED_COMMAND) != CORE1_COMMAND_STREAM) {
        return;
    }
    StreamRegion region = core1_region_vorbis();
    region.request_clock = core1_request_clock;
    core1_stream_init(&stream, region, core1_vorbis_decoder(&vorbis, region));
    core1_idle_start();
    for (;;) {
        if (!core1_stream_step(&stream)) {
            core1_idle();
        }
    }
}
