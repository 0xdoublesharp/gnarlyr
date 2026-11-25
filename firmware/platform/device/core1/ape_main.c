#include <stdint.h>

#include "core1_region.h"
#include "device/ape_accel.h"
#include "device/core1_ape.h"
#include "device/core1_control.h"
#include "idle.h"
#include "registers.h"

void core1_main(void);

static Core1Ape ape;
static Core1Stream stream;
static ApeAccel accel;

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

static void configure_accel(void)
{
    ape_accel_start(&accel, &ACCEL_BUS);
    if (accel.verified != 0) {
        demac_filter_set_hook(ape_accel_filter, &accel);
    }
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
    StreamRegion region = core1_region();
    region.request_clock = core1_request_clock;
    region.ring_bytes = APE_RING_BYTES;
    uint8_t *snapshot = (uint8_t *)(uintptr_t)(CORE1_STREAM_RING + APE_RING_BYTES);
    configure_accel();
    core1_stream_init(&stream, region, core1_ape_decoder(&ape, snapshot, region));
    core1_idle_start();
    core1_serve(&stream);
}
