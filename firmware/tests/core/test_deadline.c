#include "check.h"
#include "core/deadline.h"

static void a_deadline_is_reached_at_its_exact_time(void)
{
    CHECK(deadline_reached(5000, 5000));
}

static void a_deadline_is_not_reached_before_its_time(void)
{
    CHECK(!deadline_reached(4999, 5000));
}

static void a_deadline_stays_reached_after_its_time(void)
{
    CHECK(deadline_reached(9000, 5000));
}

static void a_deadline_past_the_clock_wrap_is_not_reached_before_the_wrap(void)
{
    uint32_t now_ms = UINT32_MAX - 50;
    uint32_t deadline_ms = now_ms + 300;
    CHECK(!deadline_reached(now_ms, deadline_ms));
    CHECK(deadline_reached(deadline_ms, deadline_ms));
}

static void a_span_has_elapsed_at_its_exact_length(void)
{
    CHECK(deadline_elapsed(5300, 5000, 300));
    CHECK(!deadline_elapsed(5299, 5000, 300));
}

static void a_span_started_before_the_clock_wrap_elapses_after_it(void)
{
    uint32_t start = UINT32_MAX - 50;
    CHECK(!deadline_elapsed(start + 299, start, 300));
    CHECK(deadline_elapsed(start + 300, start, 300));
    CHECK(deadline_elapsed(start + 9000, start, 300));
}

static void an_elapsed_span_stays_elapsed_until_the_counter_comes_round(void)
{
    CHECK(deadline_elapsed(5000 + UINT32_MAX, 5000, 300));
    CHECK(!deadline_elapsed(5000 + 299, 5000, 300));
}

static void an_empty_span_has_always_elapsed(void)
{
    CHECK(deadline_elapsed(0, 0, 0));
    CHECK(deadline_elapsed(0, UINT32_MAX, 0));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_deadline_is_reached_at_its_exact_time),
        TEST_CASE(a_deadline_is_not_reached_before_its_time),
        TEST_CASE(a_deadline_stays_reached_after_its_time),
        TEST_CASE(a_deadline_past_the_clock_wrap_is_not_reached_before_the_wrap),
        TEST_CASE(a_span_has_elapsed_at_its_exact_length),
        TEST_CASE(a_span_started_before_the_clock_wrap_elapses_after_it),
        TEST_CASE(an_elapsed_span_stays_elapsed_until_the_counter_comes_round),
        TEST_CASE(an_empty_span_has_always_elapsed),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
