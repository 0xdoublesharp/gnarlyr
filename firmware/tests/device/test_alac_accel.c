#include <string.h>

#include "check.h"
#include "device/alac_accel.h"

enum {
    CRU_GATE = 0x4018008C,
    CRU_DMA_RESET = 0x401800CC,
    HIFIACC = 0x01060000,
    COEFFICIENTS = 0x01069000,
    DMAC = 0x01070000,
    CHANNEL_STRIDE = 0x58,
    GATE_BIT = 1 << 9
};

typedef struct FakeJob {
    uint32_t count;
    uint32_t order;
    uint32_t quant;
    uint32_t width;
    int16_t coefficients[30];
} FakeJob;

typedef struct Fake {
    FakeJob job;
    uint32_t registers[2][CHANNEL_STRIDE / sizeof(uint32_t)];
    uint32_t gate;
    uint32_t control;
    uint32_t jobs;
    uint32_t stops;
    uint32_t dma_resets;
    bool unmodeled_gate;
    bool omit_last_adaptation;
    bool wrong_pcm;
    bool stall;
    bool started;
    bool dma_enabled;
    bool input_enabled;
    bool done;
} Fake;

static AlacAccel accel;
static int32_t input[ALAC_MAX_FRAME_SAMPLES];
static int32_t output[ALAC_MAX_FRAME_SAMPLES];
static Fake fake;

static uint32_t address_of(const void *pointer)
{
    return (uint32_t)(uintptr_t)pointer;
}

static int32_t sign_of(int32_t value)
{
    return (value > 0) - (value < 0);
}

static void adapt(FakeJob *job, const int32_t *window, int32_t error, bool omit_last)
{
    int32_t sign = sign_of(error);
    for (uint32_t tap = 0; tap < job->order && sign_of(error) == sign && sign != 0; tap++) {
        int32_t difference = window[0] - window[tap + 1];
        int32_t step = sign_of(difference) * sign;
        if (!omit_last || tap != job->order - 1) {
            job->coefficients[tap] = (int16_t)(job->coefficients[tap] - step);
        }
        error -= ((difference * step) >> job->quant) * (int32_t)(tap + 1);
    }
}

static int32_t predict(FakeJob *job, const int32_t *window, int32_t error)
{
    uint32_t sum = job->quant == 0 ? 0 : 1u << (job->quant - 1);
    for (uint32_t tap = 0; tap < job->order; tap++) {
        sum += (uint32_t)(window[tap + 1] - window[0]) * (uint32_t)job->coefficients[tap];
    }
    uint32_t sample =
        (uint32_t)((int32_t)sum >> job->quant) + (uint32_t)window[0] + (uint32_t)error;
    uint32_t shift = 32 - job->width;
    return (int32_t)(sample << shift) >> shift;
}

static void verify_channel(Fake *state, bool in)
{
    const AlacAccelLink *links = in ? accel.input_dma : accel.output_dma;
    const uint32_t *regs = state->registers[in ? 1 : 0];
    bool chained = state->job.count > 2048u;
    CHECK_EQUAL(in ? address_of(input) : HIFIACC + 0xA0u, regs[0x00 / 4]);
    CHECK_EQUAL(in ? HIFIACC + 0x40u : address_of(output), regs[0x08 / 4]);
    CHECK_EQUAL(chained ? address_of(links) : 0, regs[0x10 / 4]);
    CHECK_EQUAL(in ? 0x800 : 0x400, regs[0x40 / 4]);
    CHECK_EQUAL(in ? 0x807 : 7, regs[0x44 / 4]);
    uint32_t single_ctl = in ? 0x02109125u : 0x00A09425u;
    CHECK_EQUAL(single_ctl | (chained ? 0x18000000u : 0), regs[0x18 / 4]);
    CHECK_EQUAL(chained ? 2048u : state->job.count, regs[0x1C / 4]);
    if (chained) {
        CHECK_EQUAL(address_of(links + 1), links[0].next);
        CHECK_EQUAL(in ? address_of(input + 2048) : HIFIACC + 0xA0u, links[1].source);
        CHECK_EQUAL(in ? HIFIACC + 0x40u : address_of(output + 2048), links[1].destination);
        CHECK_EQUAL(state->job.count - 2048u, links[1].length);
        CHECK_EQUAL(single_ctl, links[1].control);
        CHECK_EQUAL(0, links[1].next);
    }
}

static void transfer(Fake *state)
{
    FakeJob *job = &state->job;
    CHECK(state->started);
    CHECK(state->dma_enabled);
    CHECK(state->input_enabled);
    CHECK((state->control & 0x8000u) != 0);
    verify_channel(state, true);
    verify_channel(state, false);
    state->jobs++;
    output[0] = input[0];
    if (state->stall) {
        output[1] = 123456;
        return;
    }
    memcpy(output, input, (job->order + 1u) * sizeof(*output));
    for (uint32_t i = job->order + 1u; i < job->count; i++) {
        const int32_t *window = output + i - job->order - 1;
        output[i] = predict(job, window, input[i]);
        adapt(job, window, input[i], state->omit_last_adaptation && job->order == 8);
    }
    output[0] += state->wrong_pcm;
    state->done = true;
}

static uint32_t read_reg(void *context, uint32_t address)
{
    Fake *state = context;
    if (address == CRU_GATE) {
        return state->gate;
    }
    if (address == HIFIACC + 0x04u) {
        return state->control;
    }
    if (state->done) {
        return address == DMAC + 0x2C0u ? 3u : 0x8000u;
    }
    return 0;
}

static void write_job(Fake *state, uint32_t value)
{
    if ((value >> 29) == 3) {
        state->job.count = (value >> 15) & 0x1FFFu;
        state->started = (value & 3u) == 3;
        state->done = false;
    } else {
        state->started = false;
    }
}

static void write_reg(void *context, uint32_t address, uint32_t value)
{
    Fake *state = context;
    if (address == CRU_GATE) {
        uint32_t mask = value >> 16;
        state->gate = state->unmodeled_gate ? value : (state->gate & ~mask) | (value & mask);
    } else if (address == CRU_DMA_RESET && value == 0x00800080u) {
        state->dma_resets++;
    } else if (address == HIFIACC + 0x04u) {
        state->control = value;
    } else if (address == HIFIACC + 0x0Cu) {
        write_job(state, value);
    } else if (address == HIFIACC + 0x28u) {
        state->job.order = (value >> 16) & 0xFFu;
        state->job.width = value >> 24;
        state->job.quant = (value >> 8) & 0xFFu;
    } else if (address >= COEFFICIENTS &&
               address < COEFFICIENTS + sizeof(state->job.coefficients) * 2) {
        CHECK_EQUAL((uint32_t)(int16_t)value, value);
        state->job.coefficients[(address - COEFFICIENTS) / 4] = (int16_t)value;
    } else if (address >= DMAC && address < DMAC + 2 * CHANNEL_STRIDE) {
        uint32_t offset = address - DMAC;
        state->registers[offset / CHANNEL_STRIDE][(offset % CHANNEL_STRIDE) / 4] = value;
    } else if (address == DMAC + 0x398u) {
        state->dma_enabled = value == 1;
    } else if (address == DMAC + 0x3A0u && value == 0x202u) {
        state->input_enabled = true;
    } else if (address == DMAC + 0x3A0u && value == 0x101u) {
        transfer(state);
        state->input_enabled = false;
    } else if (address == DMAC + 0x3A0u && value == 0x300u) {
        state->stops++;
    }
}

static AlacLpcHook start(Fake initial)
{
    static const AccelBus BUS = {read_reg, write_reg, &fake};
    fake = initial;
    alac_accel_start(&accel, &BUS, input, output);
    accel.output = (AlacAccelOutput){output, ALAC_MAX_FRAME_SAMPLES};
    return alac_accel_hook(&accel);
}

static AlacLpcJob small_job(void)
{
    static const int16_t COEFS[] = {4, -5, 2};
    return (AlacLpcJob){
        .coefficients = COEFS, .count = 8, .order = 3, .quant = 2, .sample_bits = 16};
}

static void an_unmodeled_clock_gate_keeps_the_hook_off(void)
{
    CHECK(start((Fake){.unmodeled_gate = true}).restore == NULL);
    CHECK_EQUAL(0, fake.jobs);
    CHECK_EQUAL(0, fake.control);
    CHECK_EQUAL(0, fake.dma_resets);
}

static void a_pcm_mismatch_keeps_the_hook_off(void)
{
    CHECK(start((Fake){.wrong_pcm = true}).restore == NULL);
    CHECK_EQUAL(1, fake.jobs);
}

static void a_self_test_timeout_stops_dma_and_keeps_the_hook_off(void)
{
    CHECK(start((Fake){.stall = true}).restore == NULL);
    CHECK_EQUAL(1, fake.jobs);
    CHECK_EQUAL(1, fake.stops);
    CHECK_EQUAL(2, fake.dma_resets);
}

static void a_wrong_adapted_coefficient_fails_the_order_eight_test(void)
{
    CHECK(start((Fake){.omit_last_adaptation = true}).restore == NULL);
    CHECK_EQUAL(2, fake.jobs);
}

static void matching_self_tests_enable_bit_exact_restore(void)
{
    static const int32_t RESIDUALS[] = {10, 11, 12, 13, 5, -2, 0, 1};
    static const int32_t EXPECTED[] = {10, 11, 12, 13, 15, 11, 9, 16};
    AlacLpcHook hook = start((Fake){0});
    CHECK(hook.restore != NULL);
    CHECK_EQUAL(2, fake.jobs);
    if (hook.restore != NULL) {
        memcpy(input, RESIDUALS, sizeof RESIDUALS);
        CHECK(hook.restore(hook.context, input, small_job()));
        CHECK(memcmp(input, EXPECTED, sizeof EXPECTED) == 0);
        CHECK_EQUAL(3, fake.jobs);
    }
}

static void unsupported_blocks_leave_the_input_alone(void)
{
    AlacLpcHook hook = start((Fake){0});
    CHECK(hook.restore != NULL);
    if (hook.restore == NULL) {
        return;
    }
    AlacLpcJob job = small_job();
    job.order = 2;
    CHECK(!hook.restore(hook.context, input, job));
    job.order = 31;
    CHECK(!hook.restore(hook.context, input, job));
    job = small_job();
    job.quant = 12;
    CHECK(!hook.restore(hook.context, input, job));
    job = small_job();
    job.count = 8192;
    CHECK(!hook.restore(hook.context, input, job));
    accel.output.capacity = 7;
    CHECK(!hook.restore(hook.context, input, small_job()));
    CHECK_EQUAL(2, fake.jobs);
}

static void a_partial_dma_timeout_preserves_the_residuals_and_disables_the_hook(void)
{
    static const int32_t RESIDUALS[] = {10, 11, 12, 13, 5, -2, 0, 1};
    AlacLpcHook hook = start((Fake){0});
    CHECK(hook.restore != NULL);
    if (hook.restore == NULL) {
        return;
    }
    fake.stall = true;
    memcpy(input, RESIDUALS, sizeof RESIDUALS);
    CHECK(!hook.restore(hook.context, input, small_job()));
    CHECK_EQUAL(123456, output[1]);
    CHECK(memcmp(input, RESIDUALS, sizeof RESIDUALS) == 0);
    CHECK(!hook.restore(hook.context, input, small_job()));
    CHECK(alac_accel_hook(&accel).restore == NULL);
    CHECK_EQUAL(3, fake.jobs);
    CHECK_EQUAL(1, fake.stops);
    CHECK_EQUAL(2, fake.dma_resets);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_unmodeled_clock_gate_keeps_the_hook_off),
        TEST_CASE(a_pcm_mismatch_keeps_the_hook_off),
        TEST_CASE(a_self_test_timeout_stops_dma_and_keeps_the_hook_off),
        TEST_CASE(a_wrong_adapted_coefficient_fails_the_order_eight_test),
        TEST_CASE(matching_self_tests_enable_bit_exact_restore),
        TEST_CASE(unsupported_blocks_leave_the_input_alone),
        TEST_CASE(a_partial_dma_timeout_preserves_the_residuals_and_disables_the_hook),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
