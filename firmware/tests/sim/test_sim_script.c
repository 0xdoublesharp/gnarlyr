#include "check.h"
#include "sim_script.h"

static void a_key_list_becomes_steps(void)
{
    SimScript script;
    CHECK(sim_script_parse("up,back:long", &script));
    CHECK_EQUAL(2, script.count);
    CHECK_EQUAL(KEY_VOLUME_DOWN, script.steps[0].key);
    CHECK_EQUAL(KEY_SCRIPT_LONG, script.steps[1].hold);
}

static void a_bad_list_is_rejected(void)
{
    SimScript script;
    CHECK(!sim_script_parse("play,jump", &script));
    CHECK(!sim_script_parse("", &script));
}

static void too_many_steps_are_rejected(void)
{
    char text[SIM_SCRIPT_MAX_STEPS * 3 + 8];
    int length = 0;
    for (int step = 0; step <= SIM_SCRIPT_MAX_STEPS; step++) {
        text[length++] = 'u';
        text[length++] = 'p';
        text[length++] = ',';
    }
    text[length - 1] = '\0';
    SimScript script;
    CHECK(!sim_script_parse(text, &script));
}

static void taps_press_and_release_then_leave_a_gap(void)
{
    SimScript script;
    sim_script_parse("play,down", &script);
    SimScriptEvent events[SIM_SCRIPT_MAX_EVENTS];
    CHECK_EQUAL(4, sim_script_schedule(&script, 10, events));
    CHECK_EQUAL(10, events[0].frame);
    CHECK(events[0].pressed);
    CHECK_EQUAL(10 + SIM_SCRIPT_TAP_FRAMES, events[1].frame);
    CHECK(!events[1].pressed);
    CHECK_EQUAL(10 + SIM_SCRIPT_TAP_FRAMES + SIM_SCRIPT_GAP_FRAMES, events[2].frame);
    CHECK_EQUAL(KEY_VOLUME_UP, events[2].key);
}

static void long_presses_hold_for_the_hold_time(void)
{
    SimScript script;
    sim_script_parse("back:long", &script);
    SimScriptEvent events[SIM_SCRIPT_MAX_EVENTS];
    CHECK_EQUAL(2, sim_script_schedule(&script, 0, events));
    CHECK_EQUAL(SIM_SCRIPT_HOLD_FRAMES, events[1].frame);
    CHECK(!events[1].pressed);
}

static void a_held_suffix_holds_for_seconds(void)
{
    SimScript script;
    CHECK(sim_script_parse("down:held", &script));
    CHECK_EQUAL(KEY_SCRIPT_HELD, script.steps[0].hold);
    SimScriptEvent events[SIM_SCRIPT_MAX_EVENTS];
    CHECK_EQUAL(2, sim_script_schedule(&script, 0, events));
    CHECK_EQUAL(KEY_VOLUME_UP, events[0].key);
    CHECK_EQUAL(SIM_SCRIPT_HELD_FRAMES, events[1].frame);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_key_list_becomes_steps),
        TEST_CASE(a_bad_list_is_rejected),
        TEST_CASE(too_many_steps_are_rejected),
        TEST_CASE(a_held_suffix_holds_for_seconds),
        TEST_CASE(taps_press_and_release_then_leave_a_gap),
        TEST_CASE(long_presses_hold_for_the_hold_time),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
