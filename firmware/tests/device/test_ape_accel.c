#include <string.h>

#include "check.h"
#include "device/ape_accel.h"

enum {
    HIFIACC = 0x01060000,
    DMAC = 0x01070000,
    CRU_GATE = 0x4018008C,
    CRU_DMA_RESET = 0x401800CC,
    CHANNEL_STRIDE = 0x58,
    PRESET_COUNT = 6,
    JOBS_PER_PRESET = 8,
    ALL_PRESETS = (1u << PRESET_COUNT) - 1,
    MODERN_NORMAL = 3,
    MODERN_EXTRA = 5
};

typedef struct ExpectedJob {
    uint32_t config;
    uint32_t count;
    uint32_t channel;
    int32_t input[APE_CHUNK_SAMPLES];
    int32_t output[APE_CHUNK_SAMPLES];
} ExpectedJob;

typedef struct Fake {
    uint32_t dma[2][CHANNEL_STRIDE / 4];
    uint32_t gate;
    uint32_t command;
    uint32_t config;
    uint32_t fifo_config;
    uint32_t fifo_interrupts;
    uint32_t fifo_clear;
    uint32_t done;
    unsigned next[PRESET_COUNT];
    unsigned polls;
    unsigned stops;
    unsigned resets;
    unsigned jobs;
    int mismatch_preset;
    int timeout_preset;
    uint32_t timeout_channel;
    bool unmodeled_gate;
    bool runtime;
    bool dma_enabled;
    bool input_enabled;
} Fake;

static ApeAccel accel;
static Fake fake;
static ExpectedJob oracle[PRESET_COUNT][JOBS_PER_PRESET];
static filter_int stage1[(64 * 3 + FILTER_HISTORY_SIZE) * 2];
static filter_int stage2[(256 * 3 + FILTER_HISTORY_SIZE) * 2];
static int32_t samples[2][APE_CHUNK_SAMPLES];

static void oracle_init(int preset)
{
    switch (preset % 3) {
    case 0:
        init_filter_16_11(stage1);
        break;
    case 1:
        init_filter_64_11(stage1);
        break;
    case 2:
        init_filter_32_10(stage1);
        init_filter_256_13(stage2);
        break;
    }
}

static void oracle_apply(int preset, int version, ExpectedJob *job)
{
    int channel = (int)job->channel;
    int count = (int)job->count;
    switch (preset % 3) {
    case 0:
        apply_filter_16_11(version, channel, job->output, count);
        break;
    case 1:
        apply_filter_64_11(version, channel, job->output, count);
        break;
    case 2:
        apply_filter_32_10(version, channel, job->output, count);
        apply_filter_256_13(version, channel, job->output, count);
        break;
    }
}

static void oracle_job(int preset, unsigned index)
{
    static const uint32_t CONFIGS[] = {0x040B0000u, 0x100B0000u, 0x280AD000u};
    ExpectedJob *job = &oracle[preset][index];
    int version = preset >= MODERN_NORMAL ? 3980 : 3970;
    unsigned chunk = index / 2;
    bool frame_start = index == 0 || index == 6;
    if (frame_start) {
        oracle_init(preset);
    }
    job->config = CONFIGS[preset % 3] | (version == 3980 ? 0x80000000u : 0u) |
                  (frame_start ? 0x00200000u : 0u);
    job->count = chunk < 2 ? APE_CHUNK_SAMPLES : 17;
    job->channel = index % 2;
    uint32_t offset = chunk < 3 ? chunk * APE_CHUNK_SAMPLES : 0;
    ape_accel_test_input(job->input, job->count, offset, job->channel);
    memcpy(job->output, job->input, job->count * sizeof(*job->input));
    oracle_apply(preset, version, job);
}

static uint32_t dma_address(const void *pointer)
{
    return (uint32_t)(uintptr_t)pointer;
}

static void verify_dma(const Fake *state, const ExpectedJob *job, const int32_t *input,
                       int32_t *output)
{
    const uint32_t *in = state->dma[1];
    const uint32_t *out = state->dma[0];
    CHECK_EQUAL(dma_address(input), in[0x00 / 4]);
    CHECK_EQUAL(HIFIACC + 0x40u, in[0x08 / 4]);
    CHECK_EQUAL(0x02109125u, in[0x18 / 4]);
    CHECK_EQUAL(0x800u, in[0x40 / 4]);
    CHECK_EQUAL(0x807u, in[0x44 / 4]);
    CHECK_EQUAL(HIFIACC + 0xA0u, out[0x00 / 4]);
    CHECK_EQUAL(dma_address(output), out[0x08 / 4]);
    CHECK_EQUAL(0x00A09425u, out[0x18 / 4]);
    CHECK_EQUAL(0x400u, out[0x40 / 4]);
    CHECK_EQUAL(7u, out[0x44 / 4]);
    for (unsigned channel = 0; channel < 2; channel++) {
        CHECK_EQUAL(0, state->dma[channel][0x10 / 4]);
        CHECK_EQUAL(job->count, state->dma[channel][0x1C / 4]);
    }
}

static int selected_preset(uint32_t config)
{
    uint32_t level = config & ~(0x80000000u | 0x00200000u);
    int version = (config & 0x80000000u) != 0 ? MODERN_NORMAL : 0;
    switch (level) {
    case 0x040B0000u:
        return version;
    case 0x100B0000u:
        return version + 1;
    case 0x280AD000u:
        return version + 2;
    default:
        return -1;
    }
}

static void dma_transfer(Fake *state)
{
    int preset = selected_preset(state->config);
    CHECK(preset >= 0);
    if (preset < 0 || state->next[preset] >= JOBS_PER_PRESET) {
        CHECK(false);
        return;
    }
    unsigned index = state->next[preset]++;
    const ExpectedJob *job = &oracle[preset][index];
    const int32_t *input = state->runtime ? samples[job->channel] : accel.output[1];
    int32_t *output = accel.output[state->runtime ? job->channel : 0];
    CHECK(state->dma_enabled && state->input_enabled);
    CHECK_EQUAL(0x01080108u, state->fifo_config);
    CHECK_EQUAL(0x00830083u, state->fifo_interrupts);
    CHECK_EQUAL(3u, state->fifo_clear);
    CHECK_EQUAL(job->config, state->config);
    CHECK_EQUAL((job->count << 15) | (job->channel << 29) | 3u, state->command);
    verify_dma(state, job, input, output);
    CHECK(memcmp(job->input, input, job->count * sizeof(*input)) == 0);
    state->jobs++;
    if (preset == state->timeout_preset && job->channel == state->timeout_channel) {
        output[0] = 1234567;
        state->done = 2;
        return;
    }
    memcpy(output, job->output, job->count * sizeof(*output));
    if (preset == state->mismatch_preset && index == 5) {
        output[job->count - 1] ^= 1;
    }
    state->done = 3;
}

static uint32_t read_register(void *context, uint32_t address)
{
    Fake *state = context;
    if (address == CRU_GATE) {
        return state->gate;
    }
    if (address == DMAC + 0x2C0u) {
        state->polls++;
        return state->done;
    }
    return 0;
}

static void write_enable(Fake *state, uint32_t value)
{
    if (value == 0x202u) {
        state->input_enabled = true;
    } else if (value == 0x101u) {
        dma_transfer(state);
    } else {
        CHECK_EQUAL(0x300u, value);
        state->input_enabled = false;
        state->stops++;
    }
}

static void write_register(void *context, uint32_t address, uint32_t value)
{
    Fake *state = context;
    if (address >= DMAC && address < DMAC + 2 * CHANNEL_STRIDE) {
        uint32_t offset = address - DMAC;
        state->dma[offset / CHANNEL_STRIDE][offset % CHANNEL_STRIDE / 4] = value;
        return;
    }
    switch (address) {
    case CRU_GATE:
        state->gate = state->unmodeled_gate ? value : 0;
        break;
    case CRU_DMA_RESET:
        state->resets += value == 0x00800080u;
        break;
    case HIFIACC:
        state->fifo_config = value;
        break;
    case HIFIACC + 4:
        state->fifo_interrupts = value;
        break;
    case HIFIACC + 0x0C:
        if ((value & 3u) != 0) {
            CHECK_EQUAL(state->command | 3u, value);
        }
        state->command = value;
        break;
    case HIFIACC + 0x10:
        state->fifo_clear = value;
        break;
    case HIFIACC + 0x20:
        state->config = value;
        break;
    case DMAC + 0x338:
        CHECK_EQUAL(3u, value);
        state->done = 0;
        break;
    case DMAC + 0x398:
        state->dma_enabled = value == 1;
        break;
    case DMAC + 0x3A0:
        write_enable(state, value);
        break;
    }
}

static const AccelBus BUS = {read_register, write_register, &fake};

static void prepare(void)
{
    fake = (Fake){.mismatch_preset = -1, .timeout_preset = -1};
    demac_filter_set_hook(NULL, NULL);
    for (int preset = 0; preset < PRESET_COUNT; preset++) {
        for (unsigned index = 0; index < JOBS_PER_PRESET; index++) {
            oracle_job(preset, index);
        }
    }
}

static void matching_demac_vectors_verify_all_presets(void)
{
    prepare();
    const ExpectedJob *edge = &oracle[MODERN_NORMAL][0];
    static const int32_t RESIDUALS[] = {257, -32, 0, 32767, 32768, -32768, -32769};
    CHECK(memcmp(edge->input, RESIDUALS, sizeof RESIDUALS) == 0);
    CHECK_EQUAL(257, edge->output[0]);
    CHECK_EQUAL(-32, edge->output[1]);
    CHECK_EQUAL(1, edge->output[2]);
    ape_accel_start(&accel, &BUS);
    CHECK_EQUAL(ALL_PRESETS, accel.verified);
    CHECK_EQUAL(PRESET_COUNT * JOBS_PER_PRESET, fake.jobs);
    CHECK_EQUAL(fake.jobs, fake.polls);
    CHECK_EQUAL(0, fake.stops);
}

static void missing_hardware_leaves_all_presets_in_software(void)
{
    prepare();
    fake.unmodeled_gate = true;
    ape_accel_start(&accel, &BUS);
    CHECK_EQUAL(0, accel.verified);
    CHECK_EQUAL(0, fake.resets);
    CHECK_EQUAL(0, fake.jobs);
}

static void start_runtime(int preset)
{
    memset(fake.next, 0, sizeof fake.next);
    fake.runtime = true;
    demac_filter_init((preset % 3 + 2) * 1000);
    demac_filter_set_hook(ape_accel_filter, &accel);
}

static void check_stereo_chunk(int preset, unsigned chunk)
{
    const ExpectedJob *left = &oracle[preset][2 * chunk];
    const ExpectedJob *right = left + 1;
    memcpy(samples[0], left->input, left->count * sizeof(*left->input));
    memcpy(samples[1], right->input, right->count * sizeof(*right->input));
    demac_filter_decode((DemacFilterJob){.level = (preset % 3 + 2) * 1000,
                                         .version = preset >= MODERN_NORMAL ? 3980 : 3970,
                                         .count = (int)left->count,
                                         .frame_start = chunk == 0 || chunk == 3,
                                         .samples = {samples[0], samples[1]}});
    CHECK(memcmp(samples[0], left->output, left->count * sizeof(*left->output)) == 0);
    CHECK(memcmp(samples[1], right->output, right->count * sizeof(*right->output)) == 0);
}

static void one_lsb_mismatch_disables_only_that_preset_and_version(void)
{
    prepare();
    fake.mismatch_preset = MODERN_EXTRA;
    ape_accel_start(&accel, &BUS);
    CHECK_EQUAL(ALL_PRESETS ^ (1u << MODERN_EXTRA), accel.verified);
    CHECK_EQUAL(6, fake.next[MODERN_EXTRA]);
    unsigned tested_jobs = fake.jobs;
    start_runtime(MODERN_EXTRA);
    check_stereo_chunk(MODERN_EXTRA, 0);
    check_stereo_chunk(MODERN_EXTRA, 1);
    CHECK_EQUAL(tested_jobs, fake.jobs);
}

static void self_test_timeout_recovers_and_tests_the_other_presets(void)
{
    prepare();
    fake.timeout_preset = MODERN_NORMAL;
    fake.timeout_channel = 1;
    ape_accel_start(&accel, &BUS);
    CHECK_EQUAL(ALL_PRESETS ^ (1u << MODERN_NORMAL), accel.verified);
    CHECK_EQUAL(1, fake.stops);
    CHECK_EQUAL(2, fake.resets);
    CHECK_EQUAL(400000u + fake.jobs - 1u, fake.polls);
}

static void stereo_timeout_after_split_jobs_uses_current_software_state(void)
{
    prepare();
    ape_accel_start(&accel, &BUS);
    for (int preset = 0; preset < PRESET_COUNT; preset++) {
        start_runtime(preset);
        check_stereo_chunk(preset, 0);
        check_stereo_chunk(preset, 1);
        fake.timeout_preset = preset;
        fake.timeout_channel = 1;
        check_stereo_chunk(preset, 2);
        CHECK_EQUAL(ALL_PRESETS & ~((1u << (unsigned)(preset + 1)) - 1u), accel.verified);
        CHECK_EQUAL(6, fake.next[preset]);
        demac_filter_init((preset % 3 + 2) * 1000);
        check_stereo_chunk(preset, 3);
        CHECK_EQUAL(6, fake.next[preset]);
    }
    CHECK_EQUAL(PRESET_COUNT, fake.stops);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(matching_demac_vectors_verify_all_presets),
        TEST_CASE(missing_hardware_leaves_all_presets_in_software),
        TEST_CASE(one_lsb_mismatch_disables_only_that_preset_and_version),
        TEST_CASE(self_test_timeout_recovers_and_tests_the_other_presets),
        TEST_CASE(stereo_timeout_after_split_jobs_uses_current_software_state),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
