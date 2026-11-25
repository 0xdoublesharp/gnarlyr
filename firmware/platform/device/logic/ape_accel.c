#include "device/ape_accel.h"

#include <string.h>

#define CRU_GATES_3 0x4018008Cu
#define HIFIACC_GATE (1u << 9)
#define ACCEL_JOB (FLAC_ACCEL_BASE + 0x0Cu)
#define ACCEL_APE_CFG (FLAC_ACCEL_BASE + 0x20u)
#define ACCEL_INPUT (FLAC_ACCEL_BASE + 0x40u)
#define ACCEL_OUTPUT (FLAC_ACCEL_BASE + 0xA0u)
#define APE_NEW_ADAPTATION (1u << 31)
#define APE_CLEAR_STATE (1u << 21)
#define DMAC_RAW_TFR (FLAC_ACCEL_DMAC + 0x2C0u)
#define DMAC_RAW_ERROR (FLAC_ACCEL_DMAC + 0x2E0u)
#define DMAC_CLEAR_TFR (FLAC_ACCEL_DMAC + 0x338u)
#define DMAC_CLEAR_ERROR (FLAC_ACCEL_DMAC + 0x358u)
#define DMAC_CONFIG (FLAC_ACCEL_DMAC + 0x398u)
#define DMAC_ENABLE (FLAC_ACCEL_DMAC + 0x3A0u)

enum {
    POLL_LIMIT = 400000,
    OUTPUT_CHANNEL = 0,
    INPUT_CHANNEL = 1,
    BOTH_CHANNELS = 3,
    INPUT_ON = 0x202,
    OUTPUT_ON = 0x101,
    BOTH_OFF = 0x300,
    CHANNEL_STRIDE = 0x58,
    JOB_LENGTH_SHIFT = 15,
    JOB_CHANNEL_SHIFT = 29,
    JOB_START = 3,
    TEST_TAIL = 17
};

static const uint32_t PRESETS[] = {0x040B0000u, 0x100B0000u, 0x280AD000u};

typedef struct ApeDmaJob {
    const int32_t *input;
    int32_t *output;
    uint32_t count;
    uint32_t channel;
    uint32_t config;
} ApeDmaJob;

static bool clock_available(const AccelBus *bus)
{
    bus->write(bus->context, CRU_GATES_3, HIFIACC_GATE << 16);
    bus->write(bus->context, CRU_GATES_3, HIFIACC_GATE);
    return (bus->read(bus->context, CRU_GATES_3) & HIFIACC_GATE) == 0;
}

static uint32_t preset_mask(int level, int version)
{
    return 1u << (unsigned)((level / 1000 - 2) + (version >= 3980 ? 3 : 0));
}

static uint32_t preset_config(DemacFilterJob job)
{
    return PRESETS[job.level / 1000 - 2] | (job.version >= 3980 ? APE_NEW_ADAPTATION : 0u) |
           (job.frame_start ? APE_CLEAR_STATE : 0u);
}

static void program_dma(const AccelBus *bus, uint32_t channel, ApeDmaJob job)
{
    uint32_t base = FLAC_ACCEL_DMAC + channel * CHANNEL_STRIDE;
    bool input = channel == INPUT_CHANNEL;
    bus->write(bus->context, base + 0x00u, input ? (uint32_t)(uintptr_t)job.input : ACCEL_OUTPUT);
    bus->write(bus->context, base + 0x08u, input ? ACCEL_INPUT : (uint32_t)(uintptr_t)job.output);
    bus->write(bus->context, base + 0x10u, 0);
    bus->write(bus->context, base + 0x18u, input ? 0x02109125u : 0x00A09425u);
    bus->write(bus->context, base + 0x1Cu, job.count);
    bus->write(bus->context, base + 0x40u, input ? 0x800u : 0x400u);
    bus->write(bus->context, base + 0x44u, input ? 0x807u : 7u);
}

static bool wait_for_dma(const AccelBus *bus)
{
    for (uint32_t poll = 0; poll < POLL_LIMIT; poll++) {
        if ((bus->read(bus->context, DMAC_RAW_ERROR) & BOTH_CHANNELS) != 0) {
            return false;
        }
        if ((bus->read(bus->context, DMAC_RAW_TFR) & BOTH_CHANNELS) == BOTH_CHANNELS) {
            return true;
        }
    }
    return false;
}

static void prepare_job(const AccelBus *bus, ApeDmaJob job, uint32_t command)
{
    for (uint32_t reg = DMAC_CLEAR_TFR; reg <= DMAC_CLEAR_ERROR; reg += 8) {
        bus->write(bus->context, reg, BOTH_CHANNELS);
    }
    bus->write(bus->context, ACCEL_JOB, command);
    bus->write(bus->context, ACCEL_APE_CFG, job.config);
    program_dma(bus, INPUT_CHANNEL, job);
    program_dma(bus, OUTPUT_CHANNEL, job);
    bus->write(bus->context, DMAC_CONFIG, 1);
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static bool run_job(const AccelBus *bus, ApeDmaJob job)
{
    uint32_t command = (job.count << JOB_LENGTH_SHIFT) | (job.channel << JOB_CHANNEL_SHIFT);
    prepare_job(bus, job, command);
    bus->write(bus->context, ACCEL_JOB, command | JOB_START);
    bus->write(bus->context, DMAC_ENABLE, INPUT_ON);
    bus->write(bus->context, DMAC_ENABLE, OUTPUT_ON);
    bool done = wait_for_dma(bus);
    if (!done) {
        bus->write(bus->context, DMAC_ENABLE, BOTH_OFF);
        flac_accel_start(bus);
    }
    bus->write(bus->context, ACCEL_JOB, 0);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return done;
}

void ape_accel_test_input(int32_t *samples, uint32_t count, uint32_t offset, uint32_t channel)
{
    static const int32_t EDGES[] = {257,     -32,      0,        32767,   32768, -32768, -32769, 0,
                                    8388607, -8388608, 16777219, -500007, 0,     127,    -159,   1};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = offset + i + channel * 37u;
        uint32_t position = index & 127u;
        uint32_t mixed = (index + 1u) * 0x9E3779B9u;
        samples[i] = position < sizeof EDGES / sizeof EDGES[0]
                         ? EDGES[position]
                         : (int32_t)((mixed ^ (mixed >> 16)) & 2047u) - 1024;
    }
}

static bool test_channel(ApeAccel *accel, DemacFilterJob filter, uint32_t channel, uint32_t offset)
{
    int32_t expected[APE_CHUNK_SAMPLES];
    uint32_t count = (uint32_t)filter.count;
    ape_accel_test_input(accel->output[1], count, offset, channel);
    memcpy(expected, accel->output[1], count * sizeof(*expected));
    filter.samples[channel] = expected;
    demac_filter_apply(filter);
    for (uint32_t i = 0; i < count; i++) {
        accel->output[0][i] = expected[i] ^ 1;
    }
    ApeDmaJob job = {accel->output[1], accel->output[0], count, channel, preset_config(filter)};
    return run_job(accel->bus, job) &&
           memcmp(expected, accel->output[0], count * sizeof(*expected)) == 0;
}

static bool test_preset(ApeAccel *accel, DemacFilterJob filter)
{
    static const uint32_t OFFSETS[] = {0, APE_CHUNK_SAMPLES, 2u * APE_CHUNK_SAMPLES, 0};
    for (uint32_t i = 0; i < sizeof OFFSETS / sizeof OFFSETS[0]; i++) {
        filter.frame_start = OFFSETS[i] == 0;
        filter.count = i < 2 ? APE_CHUNK_SAMPLES : TEST_TAIL;
        if (filter.frame_start) {
            demac_filter_init(filter.level);
        }
        if (!test_channel(accel, filter, 0, OFFSETS[i])) {
            return false;
        }
        filter.frame_start = false;
        if (!test_channel(accel, filter, 1, OFFSETS[i])) {
            return false;
        }
    }
    return true;
}

void ape_accel_start(ApeAccel *accel, const AccelBus *bus)
{
    accel->bus = bus;
    accel->verified = 0;
    if (!clock_available(bus)) {
        return;
    }
    flac_accel_start(bus);
    for (int version = 3980; version >= 3970; version -= 10) {
        for (int level = 2000; level <= 4000; level += 1000) {
            if (test_preset(accel, (DemacFilterJob){.level = level, .version = version})) {
                accel->verified |= preset_mask(level, version);
            }
        }
    }
}

bool ape_accel_filter(void *context, DemacFilterJob filter)
{
    ApeAccel *accel = context;
    if (filter.level < 2000 || filter.level > 4000 || filter.level % 1000 != 0 ||
        filter.count <= 0 || filter.count > APE_CHUNK_SAMPLES) {
        return false;
    }
    uint32_t mask = preset_mask(filter.level, filter.version);
    if ((accel->verified & mask) == 0) {
        return false;
    }
    uint32_t config = preset_config(filter);
    for (uint32_t channel = 0; channel < 2; channel++, config &= ~APE_CLEAR_STATE) {
        ApeDmaJob job = {filter.samples[channel], accel->output[channel], (uint32_t)filter.count,
                         channel, config};
        if (job.input != NULL && !run_job(accel->bus, job)) {
            accel->verified &= ~mask;
            return false;
        }
    }
    const int32_t *filtered[] = {accel->output[0], accel->output[1]};
    demac_filter_observe(filter, filtered);
    return true;
}
