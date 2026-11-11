#include "check.h"
#include "playback/resume_rewind.h"

static uint32_t timed(uint32_t paused_ms, bool spoken)
{
    return resume_rewind_ms(
        (ResumeRewind){.pause = RESUME_REWIND_TIMED, .paused_ms = paused_ms, .spoken = spoken});
}

static void a_pause_of_ten_seconds_or_less_does_not_rewind(void)
{
    CHECK_EQUAL(0, timed(0, false));
    CHECK_EQUAL(0, timed(9999, false));
    CHECK_EQUAL(0, timed(10000, false));
    CHECK_EQUAL(0, timed(10000, true));
}

static void music_rewinds_three_seconds_after_a_minute(void)
{
    CHECK_EQUAL(3000, timed(60000, false));
    CHECK_EQUAL(1500, timed(35000, false));
}

static void music_rewinds_ten_seconds_after_ten_minutes(void)
{
    CHECK_EQUAL(10000, timed(600000, false));
    CHECK_EQUAL(6500, timed(330000, false));
}

static void music_rewinds_twenty_seconds_after_an_hour_and_no_more(void)
{
    CHECK_EQUAL(20000, timed(3600000, false));
    CHECK_EQUAL(20000, timed(86400000, false));
    CHECK_EQUAL(20000, timed(UINT32_MAX, false));
}

static void the_rewind_grows_with_the_pause(void)
{
    uint32_t previous = 0;
    for (uint32_t paused = 0; paused <= 4000000; paused += 1000) {
        uint32_t rewind = timed(paused, false);
        CHECK(rewind >= previous);
        CHECK(rewind - previous <= 60);
        previous = rewind;
    }
}

static void spoken_word_rewinds_half_as_much_again(void)
{
    CHECK_EQUAL(4500, timed(60000, true));
    CHECK_EQUAL(15000, timed(600000, true));
    CHECK_EQUAL(30000, timed(3600000, true));
}

static void a_cold_resume_rewinds_a_fixed_amount(void)
{
    ResumeRewind music = {.pause = RESUME_REWIND_COLD, .paused_ms = 0, .spoken = false};
    CHECK_EQUAL(RESUME_REWIND_COLD_MUSIC_MS, resume_rewind_ms(music));
    ResumeRewind spoken = {.pause = RESUME_REWIND_COLD, .paused_ms = 0, .spoken = true};
    CHECK_EQUAL(RESUME_REWIND_COLD_SPOKEN_MS, resume_rewind_ms(spoken));
}

static void no_pause_means_no_rewind(void)
{
    ResumeRewind none = {.pause = RESUME_REWIND_NONE, .paused_ms = 3600000, .spoken = true};
    CHECK_EQUAL(0, resume_rewind_ms(none));
}

static void the_rewind_never_goes_before_the_start_of_the_track(void)
{
    CHECK_EQUAL(37000, resume_rewind_position(40000, 3000));
    CHECK_EQUAL(0, resume_rewind_position(2000, 3000));
    CHECK_EQUAL(0, resume_rewind_position(3000, 3000));
    CHECK_EQUAL(0, resume_rewind_position(0, 20000));
    CHECK_EQUAL(5000, resume_rewind_position(5000, 0));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_pause_of_ten_seconds_or_less_does_not_rewind),
        TEST_CASE(music_rewinds_three_seconds_after_a_minute),
        TEST_CASE(music_rewinds_ten_seconds_after_ten_minutes),
        TEST_CASE(music_rewinds_twenty_seconds_after_an_hour_and_no_more),
        TEST_CASE(the_rewind_grows_with_the_pause),
        TEST_CASE(spoken_word_rewinds_half_as_much_again),
        TEST_CASE(a_cold_resume_rewinds_a_fixed_amount),
        TEST_CASE(no_pause_means_no_rewind),
        TEST_CASE(the_rewind_never_goes_before_the_start_of_the_track),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
