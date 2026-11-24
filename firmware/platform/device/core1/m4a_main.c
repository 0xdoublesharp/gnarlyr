#include <stddef.h>
#include <stdint.h>

#include "core1_region.h"
#include "device/alac_accel.h"
#include "device/core1_control.h"
#include "device/core1_heap.h"
#include "device/core1_m4a.h"
#include "idle.h"
#include "registers.h"

void core1_main(void);

static Core1M4a m4a;
static Core1Stream stream;
static AlacAccel accel;
static Core1DecodeResult (*decode_m4a)(void *context, Core1Block *block);

static uint32_t accel_read(void *context, uint32_t address)
{
    (void)context;
    return register_read(address);
}

static void accel_write(void *context, uint32_t address, uint32_t value)
{
    (void)context;
    register_write(address, value);
}

static const AccelBus ACCEL_BUS = {accel_read, accel_write, NULL};

static Core1DecodeResult decode_with_accel(void *context, Core1Block *block)
{
    accel.output = (AlacAccelOutput){block->out[0], block->room};
    Core1DecodeResult result = decode_m4a(context, block);
    accel.output = (AlacAccelOutput){0};
    return result;
}

static Core1Decoder configure_decoder(StreamRegion region)
{
    Core1Decoder decoder = core1_m4a_decoder(&m4a);
    alac_accel_start(&accel, &ACCEL_BUS, region.samples[0][1], region.samples[0][0]);
    AlacLpcHook hook = alac_accel_hook(&accel);
    if (hook.restore != NULL) {
        alac_set_default_lpc_hook(hook);
        decode_m4a = decoder.decode;
        decoder.decode = decode_with_accel;
    }
    return decoder;
}

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
    StreamRegion region = core1_region_m4a();
    core1_stream_init(&stream, region, configure_decoder(region));
    core1_idle_start();
    core1_serve(&stream);
}
