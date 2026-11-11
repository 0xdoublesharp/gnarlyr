#include <string.h>

#include "check.h"
#include "core/debug_log.h"

static DebugLog log_with(uint32_t count)
{
    DebugLog log;
    debug_log_reset(&log);
    for (uint32_t index = 0; index < count; index++) {
        DebugEvent event = debug_event_make(100 + index, DEBUG_EVENT_KEY, index, NULL);
        debug_log_record(&log, &event);
    }
    return log;
}

static void an_empty_log_holds_nothing(void)
{
    DebugLog log = log_with(0);
    CHECK_EQUAL(0, debug_log_count(&log));
    CHECK_EQUAL(0, debug_log_dropped(&log));
}

static void events_come_back_oldest_first(void)
{
    DebugLog log = log_with(3);
    CHECK_EQUAL(3, debug_log_count(&log));
    CHECK_EQUAL(100, debug_log_at(&log, 0)->time_ms);
    CHECK_EQUAL(2, debug_log_at(&log, 2)->value);
}

static void a_full_ring_drops_the_oldest_and_counts_them(void)
{
    DebugLog log = log_with(DEBUG_LOG_CAPACITY + 5);
    CHECK_EQUAL(DEBUG_LOG_CAPACITY, debug_log_count(&log));
    CHECK_EQUAL(5, debug_log_dropped(&log));
    CHECK_EQUAL(5, debug_log_at(&log, 0)->value);
    CHECK_EQUAL(DEBUG_LOG_CAPACITY + 4, debug_log_at(&log, DEBUG_LOG_CAPACITY - 1)->value);
}

static void the_ring_keeps_its_order_after_wrapping_many_times(void)
{
    DebugLog log = log_with(DEBUG_LOG_CAPACITY * 7 + 11);
    for (uint32_t index = 1; index < debug_log_count(&log); index++) {
        CHECK_EQUAL(debug_log_at(&log, index - 1)->value + 1, debug_log_at(&log, index)->value);
    }
}

static void an_event_keeps_the_start_of_its_text(void)
{
    DebugEvent event = debug_event_make(1, DEBUG_EVENT_SCREEN, 2, "Now Playing");
    CHECK_EQUAL(0, memcmp(event.text, "Now Pla", DEBUG_LOG_TEXT_BYTES));
    CHECK_EQUAL(DEBUG_EVENT_SCREEN, event.kind);
}

static void a_short_or_missing_text_is_zero_padded(void)
{
    DebugEvent event = debug_event_make(1, DEBUG_EVENT_SCREEN, 2, "Home");
    CHECK_EQUAL(0, memcmp(event.text, "Home\0\0\0", DEBUG_LOG_TEXT_BYTES));
    event = debug_event_make(1, DEBUG_EVENT_KEY, 2, NULL);
    CHECK_EQUAL(0, event.text[0]);
}

static void an_event_is_sixteen_bytes(void)
{
    CHECK_EQUAL(16, sizeof(DebugEvent));
}

static void the_ring_keeps_events_when_the_lifetime_count_saturates(void)
{
    DebugLog log = log_with(DEBUG_LOG_CAPACITY);
    log.recorded = UINT32_MAX;
    DebugEvent event = debug_event_make(999, DEBUG_EVENT_KEY, 77, NULL);
    debug_log_record(&log, &event);
    CHECK_EQUAL(DEBUG_LOG_CAPACITY, debug_log_count(&log));
    CHECK_EQUAL(UINT32_MAX - DEBUG_LOG_CAPACITY, debug_log_dropped(&log));
    CHECK_EQUAL(77, debug_log_at(&log, DEBUG_LOG_CAPACITY - 1)->value);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_empty_log_holds_nothing),
        TEST_CASE(events_come_back_oldest_first),
        TEST_CASE(a_full_ring_drops_the_oldest_and_counts_them),
        TEST_CASE(the_ring_keeps_its_order_after_wrapping_many_times),
        TEST_CASE(an_event_keeps_the_start_of_its_text),
        TEST_CASE(a_short_or_missing_text_is_zero_padded),
        TEST_CASE(an_event_is_sixteen_bytes),
        TEST_CASE(the_ring_keeps_events_when_the_lifetime_count_saturates),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
