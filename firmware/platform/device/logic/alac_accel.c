#include "device/alac_accel.h"

#include <stddef.h>

#define CRU_GATES_3 0x4018008Cu
#define HIFIACC_GATE (1u << 9)
#define ACCEL_CONTROL (FLAC_ACCEL_BASE + 0x04u)
#define ACCEL_STATUS (FLAC_ACCEL_BASE + 0x08u)
#define ACCEL_JOB (FLAC_ACCEL_BASE + 0x0Cu)
#define ACCEL_LPC (FLAC_ACCEL_BASE + 0x28u)
#define ACCEL_INPUT (FLAC_ACCEL_BASE + 0x40u)
#define ACCEL_OUTPUT (FLAC_ACCEL_BASE + 0xA0u)
#define ACCEL_COEFFICIENTS 0x01069000u
#define CONTROL_RUN (1u << 15)
#define STATUS_DONE (1u << 15)
#define JOB_ALAC (3u << 29)
#define JOB_START 3u
#define DMAC_RAW_TFR (FLAC_ACCEL_DMAC + 0x2C0u)
#define DMAC_CLEAR_TFR (FLAC_ACCEL_DMAC + 0x338u)
#define DMAC_CLEAR_ERROR (FLAC_ACCEL_DMAC + 0x358u)
#define DMAC_CONFIG (FLAC_ACCEL_DMAC + 0x398u)
#define DMAC_ENABLE (FLAC_ACCEL_DMAC + 0x3A0u)
#define DMA_LINK_ENABLE (3u << 27)

enum {
    JOB_LENGTH_SHIFT = 15,
    ORDER_SHIFT = 16,
    QUANT_SHIFT = 8,
    SAMPLE_BITS_SHIFT = 24,
    POLL_LIMIT = 400000,
    MAX_JOB_WORDS = 8191,
    INPUT_CHANNEL = 1,
    OUTPUT_CHANNEL = 0,
    BOTH_CHANNELS = 3,
    INPUT_ON = 0x202,
    OUTPUT_ON = 0x101,
    BOTH_OFF = 0x300,
    CHANNEL_STRIDE = 0x58
};

static void write(const AccelBus *bus, uint32_t address, uint32_t value)
{
    bus->write(bus->context, address, value);
}

static uint32_t read(const AccelBus *bus, uint32_t address)
{
    return bus->read(bus->context, address);
}

static bool hifiacc_clock_available(const AccelBus *bus)
{
    write(bus, CRU_GATES_3, HIFIACC_GATE << 16);
    write(bus, CRU_GATES_3, HIFIACC_GATE);
    return (read(bus, CRU_GATES_3) & HIFIACC_GATE) == 0;
}

static uint32_t dma_address(const void *buffer)
{
    return (uint32_t)(uintptr_t)buffer;
}

static void describe_dma(AlacAccelLink *links, uint32_t address, uint32_t count, bool input)
{
    for (uint32_t index = 0; count != 0 && index < ALAC_ACCEL_DMA_LINKS; index++) {
        uint32_t length = count < ALAC_ACCEL_DMA_WORDS ? count : ALAC_ACCEL_DMA_WORDS;
        count -= length;
        bool more = count != 0 && index + 1 < ALAC_ACCEL_DMA_LINKS;
        links[index] = (AlacAccelLink){
            .source = input ? address : ACCEL_OUTPUT,
            .destination = input ? ACCEL_INPUT : address,
            .next = more ? dma_address(&links[index + 1]) : 0,
            .control = (input ? 0x02109125u : 0x00A09425u) | (more ? DMA_LINK_ENABLE : 0),
            .length = length,
        };
        address += length * sizeof(int32_t);
    }
}

static void program_dma(const AccelBus *bus, const AlacAccelLink *links, uint32_t channel)
{
    uint32_t base = FLAC_ACCEL_DMAC + channel * CHANNEL_STRIDE;
    bool input = channel == INPUT_CHANNEL;
    write(bus, base + 0x00u, links->source);
    write(bus, base + 0x08u, links->destination);
    write(bus, base + 0x10u, links->next != 0 ? dma_address(links) : 0);
    write(bus, base + 0x18u, links->control);
    write(bus, base + 0x1Cu, links->length);
    write(bus, base + 0x40u, input ? 0x800u : 0x400u);
    write(bus, base + 0x44u, input ? 0x807u : 7u);
}

static void prepare_dma(AlacAccel *accel, const int32_t *samples, uint32_t count)
{
    describe_dma(accel->input_dma, dma_address(samples), count, true);
    describe_dma(accel->output_dma, dma_address(accel->output.samples), count, false);
    program_dma(accel->bus, accel->input_dma, INPUT_CHANNEL);
    program_dma(accel->bus, accel->output_dma, OUTPUT_CHANNEL);
    write(accel->bus, DMAC_CONFIG, 1);
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static uint32_t prepare_lpc(const AccelBus *bus, AlacLpcJob job)
{
    uint32_t command = JOB_ALAC | (job.count << JOB_LENGTH_SHIFT);
    write(bus, ACCEL_JOB, command);
    write(bus, ACCEL_CONTROL, read(bus, ACCEL_CONTROL) | CONTROL_RUN);
    write(bus, ACCEL_LPC,
          (uint32_t)job.sample_bits << SAMPLE_BITS_SHIFT | (uint32_t)job.order << ORDER_SHIFT |
              (uint32_t)job.quant << QUANT_SHIFT);
    for (uint32_t tap = 0; tap < job.order; tap++) {
        write(bus, ACCEL_COEFFICIENTS + tap * sizeof(uint32_t), (uint32_t)job.coefficients[tap]);
    }
    return command;
}

static bool wait_for_output(const AccelBus *bus)
{
    for (uint32_t poll = 0; poll < POLL_LIMIT; poll++) {
        if ((read(bus, DMAC_RAW_TFR) & 1u) != 0 && (read(bus, ACCEL_STATUS) & STATUS_DONE) != 0) {
            return true;
        }
    }
    return false;
}

static bool run_job(AlacAccel *accel, const int32_t *samples, AlacLpcJob job)
{
    const AccelBus *bus = accel->bus;
    for (uint32_t reg = DMAC_CLEAR_TFR; reg <= DMAC_CLEAR_ERROR; reg += 8) {
        write(bus, reg, BOTH_CHANNELS);
    }
    uint32_t command = prepare_lpc(bus, job);
    prepare_dma(accel, samples, job.count);
    write(bus, ACCEL_JOB, command | JOB_START);
    write(bus, DMAC_ENABLE, INPUT_ON);
    write(bus, DMAC_ENABLE, OUTPUT_ON);
    bool done = wait_for_output(bus);
    if (!done) {
        write(bus, DMAC_ENABLE, BOTH_OFF);
    }
    write(bus, ACCEL_CONTROL, read(bus, ACCEL_CONTROL) & ~CONTROL_RUN);
    write(bus, ACCEL_JOB, 0);
    if (!done) {
        flac_accel_start(bus);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return done;
}

static bool supported(const AlacAccel *accel, AlacLpcJob job)
{
    return accel->enabled && accel->output.samples != NULL && job.order >= 3 && job.order <= 30 &&
           job.quant < 12 && job.count > job.order + 1u && job.count <= MAX_JOB_WORDS &&
           job.count <= ALAC_MAX_FRAME_SAMPLES && job.count <= accel->output.capacity;
}

static bool restore(void *context, int32_t *samples, AlacLpcJob job)
{
    AlacAccel *accel = context;
    if (!supported(accel, job)) {
        return false;
    }
    if (!run_job(accel, samples, job)) {
        accel->enabled = false;
        return false;
    }
    __builtin_memcpy(samples, accel->output.samples, job.count * sizeof(*samples));
    return true;
}

static bool matches_impulse(const int32_t *samples, AlacLpcJob job, int32_t impulse)
{
    if (samples[0] != 1) {
        return false;
    }
    for (uint32_t index = 1; index < job.count; index++) {
        int32_t expected = index % (job.order + 1u) == 0 ? impulse : 0;
        if (samples[index] != expected) {
            return false;
        }
    }
    return true;
}

static bool adaptation_test(AlacAccel *accel, int32_t *input, AlacLpcJob job, int16_t sign)
{
    int16_t coefficients[8];
    for (uint32_t tap = 0; tap < job.order; tap++) {
        coefficients[tap] = sign;
    }
    job.coefficients = coefficients;
    int32_t impulse = sign * (1 << job.quant);
    for (uint32_t index = 0; index < job.count; index++) {
        input[index] = 0;
        accel->output.samples[index] = 1;
    }
    input[0] = 1;
    input[job.order + 1u] = impulse - 1;
    return run_job(accel, input, job) && matches_impulse(accel->output.samples, job, impulse);
}

void alac_accel_start(AlacAccel *accel, const AccelBus *bus, int32_t *input, int32_t *output)
{
    *accel = (AlacAccel){.bus = bus, .output = {output, ALAC_MAX_FRAME_SAMPLES}};
    if (hifiacc_clock_available(bus)) {
        flac_accel_start(bus);
        AlacLpcJob narrow = {.count = 10, .order = 4, .quant = 5, .sample_bits = 16};
        AlacLpcJob wide = {
            .count = ALAC_MAX_FRAME_SAMPLES, .order = 8, .quant = 11, .sample_bits = 25};
        accel->enabled =
            adaptation_test(accel, input, narrow, 1) && adaptation_test(accel, input, wide, -1);
    }
    accel->output = (AlacAccelOutput){0};
}

AlacLpcHook alac_accel_hook(AlacAccel *accel)
{
    return accel->enabled ? (AlacLpcHook){restore, accel} : (AlacLpcHook){0};
}
