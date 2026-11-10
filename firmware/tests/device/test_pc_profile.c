#include "check.h"
#include "device/pc_profile.h"

enum {
    CODE_BASE = 0x03050000,
    CODE_BYTES = 0x2666C,
    SHIFT = 7,
    BUCKETS = (CODE_BYTES + (1 << SHIFT) - 1) >> SHIFT,
    IDLE_BASE = CODE_BASE + 0x1234,
    IDLE_BYTES = 4,
    OVERLAY_BASE = 0x0307A000,
    OVERLAY_BYTES = 0x18000
};

static uint16_t buckets[BUCKETS + 1];

static PcProfile started(void)
{
    PcProfile profile = {.buckets = buckets,
                         .bucket_count = BUCKETS,
                         .bucket_shift = SHIFT,
                         .code = {CODE_BASE, CODE_BYTES},
                         .idle = {IDLE_BASE, IDLE_BYTES},
                         .overlay = {OVERLAY_BASE, OVERLAY_BYTES}};
    buckets[BUCKETS] = 0;
    pc_profile_reset(&profile);
    pc_profile_enable(&profile, true);
    return profile;
}

static uint32_t counted_in_buckets(void)
{
    uint32_t total = 0;
    for (int index = 0; index < BUCKETS; index++) {
        total += buckets[index];
    }
    return total;
}

static void nothing_is_counted_while_profiling_is_off(void)
{
    PcProfile profile = started();
    pc_profile_enable(&profile, false);
    pc_profile_sample(&profile, CODE_BASE + 0x100);
    CHECK_EQUAL(0, profile.samples);
    CHECK_EQUAL(0, counted_in_buckets());
}

static void a_code_address_counts_in_its_bucket(void)
{
    PcProfile profile = started();
    pc_profile_sample(&profile, CODE_BASE);
    pc_profile_sample(&profile, CODE_BASE + 127);
    pc_profile_sample(&profile, CODE_BASE + 128);
    pc_profile_sample(&profile, CODE_BASE + 5 * 128 + 6);
    CHECK_EQUAL(2, buckets[0]);
    CHECK_EQUAL(1, buckets[1]);
    CHECK_EQUAL(1, buckets[5]);
    CHECK_EQUAL(4, profile.samples);
}

static void the_thumb_bit_of_a_stacked_address_is_ignored(void)
{
    PcProfile profile = started();
    pc_profile_sample(&profile, (CODE_BASE + 127) | 1u);
    pc_profile_sample(&profile, (CODE_BASE + 128) | 1u);
    CHECK_EQUAL(1, buckets[0]);
    CHECK_EQUAL(1, buckets[1]);
}

static void the_last_byte_of_the_window_has_a_bucket_and_the_next_has_none(void)
{
    PcProfile profile = started();
    pc_profile_sample(&profile, CODE_BASE + CODE_BYTES - 2);
    pc_profile_sample(&profile, CODE_BASE + CODE_BYTES);
    CHECK_EQUAL(1, buckets[BUCKETS - 1]);
    CHECK_EQUAL(0, buckets[BUCKETS]);
    CHECK_EQUAL(1, profile.other_samples);
}

static void the_wait_for_interrupt_counts_as_idle_not_as_code(void)
{
    PcProfile profile = started();
    pc_profile_sample(&profile, (IDLE_BASE + 2) | 1u);
    pc_profile_sample(&profile, IDLE_BASE + IDLE_BYTES);
    CHECK_EQUAL(1, profile.idle_samples);
    CHECK_EQUAL(1, counted_in_buckets());
}

static void the_high_ram_window_is_one_bucket(void)
{
    PcProfile profile = started();
    pc_profile_sample(&profile, OVERLAY_BASE);
    pc_profile_sample(&profile, OVERLAY_BASE + OVERLAY_BYTES - 2);
    pc_profile_sample(&profile, OVERLAY_BASE + OVERLAY_BYTES);
    CHECK_EQUAL(2, profile.overlay_samples);
    CHECK_EQUAL(1, profile.other_samples);
    CHECK_EQUAL(0, counted_in_buckets());
}

static void addresses_below_the_code_window_count_as_other(void)
{
    PcProfile profile = started();
    pc_profile_sample(&profile, CODE_BASE - 2);
    pc_profile_sample(&profile, 0);
    pc_profile_sample(&profile, 0xFFFFFFFEu);
    CHECK_EQUAL(3, profile.other_samples);
    CHECK_EQUAL(0, counted_in_buckets());
}

static void every_sample_lands_in_exactly_one_place(void)
{
    PcProfile profile = started();
    for (uint32_t pc = CODE_BASE - 0x400; pc < OVERLAY_BASE + OVERLAY_BYTES + 0x400; pc += 0x36) {
        pc_profile_sample(&profile, pc);
    }
    CHECK_EQUAL(profile.samples, counted_in_buckets() + profile.idle_samples +
                                     profile.overlay_samples + profile.other_samples);
}

static void a_full_bucket_stops_the_profile_so_shares_stay_true(void)
{
    PcProfile profile = started();
    for (uint32_t sample = 0; sample < PC_PROFILE_BUCKET_FULL + 10u; sample++) {
        pc_profile_sample(&profile, CODE_BASE + 0x200);
    }
    CHECK_EQUAL(PC_PROFILE_BUCKET_FULL, buckets[4]);
    CHECK_EQUAL(PC_PROFILE_BUCKET_FULL, profile.samples);
    CHECK(profile.full);
    CHECK(!profile.enabled);
    pc_profile_enable(&profile, true);
    CHECK(!profile.enabled);
}

static void a_reset_clears_the_counts_and_keeps_profiling_on(void)
{
    PcProfile profile = started();
    for (uint32_t sample = 0; sample < PC_PROFILE_BUCKET_FULL; sample++) {
        pc_profile_sample(&profile, CODE_BASE);
    }
    pc_profile_sample(&profile, OVERLAY_BASE);
    pc_profile_reset(&profile);
    CHECK_EQUAL(0, counted_in_buckets());
    CHECK_EQUAL(0, profile.samples);
    CHECK(!profile.full);
    CHECK(profile.enabled);
}

static void a_reset_while_off_stays_off(void)
{
    PcProfile profile = started();
    pc_profile_enable(&profile, false);
    pc_profile_reset(&profile);
    CHECK(!profile.enabled);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(nothing_is_counted_while_profiling_is_off),
        TEST_CASE(a_code_address_counts_in_its_bucket),
        TEST_CASE(the_thumb_bit_of_a_stacked_address_is_ignored),
        TEST_CASE(the_last_byte_of_the_window_has_a_bucket_and_the_next_has_none),
        TEST_CASE(the_wait_for_interrupt_counts_as_idle_not_as_code),
        TEST_CASE(the_high_ram_window_is_one_bucket),
        TEST_CASE(addresses_below_the_code_window_count_as_other),
        TEST_CASE(every_sample_lands_in_exactly_one_place),
        TEST_CASE(a_full_bucket_stops_the_profile_so_shares_stay_true),
        TEST_CASE(a_reset_clears_the_counts_and_keeps_profiling_on),
        TEST_CASE(a_reset_while_off_stays_off),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
