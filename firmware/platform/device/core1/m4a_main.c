#include <stdint.h>

#include "core1_region.h"
#include "device/core1_control.h"
#include "device/core1_heap.h"
#include "device/core1_m4a.h"
#include "idle.h"
#include "registers.h"

void core1_main(void);

static Core1M4a m4a;
static Core1Stream stream;

void core1_main(void)
{
    core1_heap_init(core1_m4a_heap(), M4A_HEAP_BYTES);
    register_write(CORE1_SHARED_READY, CORE1_READY_MAGIC);
    __asm__ volatile("dsb" ::: "memory");
    register_write(CORE1_MAILBOX_B2A_COMMAND, CORE1_READY_COMMAND);
    register_write(CORE1_MAILBOX_B2A_DATA, 0);
    if (register_read(CORE1_SHARED_COMMAND) != CORE1_COMMAND_STREAM) {
        return;
    }
    core1_stream_init(&stream, core1_region_m4a(), core1_m4a_decoder(&m4a));
    core1_idle_start();
    for (;;) {
        if (!core1_stream_step(&stream)) {
            core1_idle();
        }
    }
}
