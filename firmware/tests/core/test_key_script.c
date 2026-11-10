#include <string.h>

#include "check.h"
#include "core/key_script.h"

enum {
    CAPACITY = 8
};

static int parse(const char *text, KeyScriptStep *steps)
{
    return key_script_parse(text, strlen(text), steps, CAPACITY);
}

static void key_names_map_to_device_keys(void)
{
    KeyScriptStep steps[CAPACITY];
    CHECK_EQUAL(6, parse("up,down,play,menu,back,power", steps));
    CHECK_EQUAL(KEY_VOLUME_DOWN, steps[0].key);
    CHECK_EQUAL(KEY_VOLUME_UP, steps[1].key);
    CHECK_EQUAL(KEY_PLAY, steps[2].key);
    CHECK_EQUAL(KEY_MENU, steps[3].key);
    CHECK_EQUAL(KEY_BACK, steps[4].key);
    CHECK_EQUAL(KEY_POWER, steps[5].key);
    CHECK_EQUAL(KEY_SCRIPT_TAP, steps[0].hold);
}

static void every_key_is_named_as_it_is_parsed(void)
{
    for (int key = 0; key < KEY_COUNT; key++) {
        KeyScriptStep step;
        const char *name = key_script_key_name((Key)key);
        CHECK(key_script_parse_step(name, strlen(name), &step));
        CHECK_EQUAL(key, step.key);
    }
}

static void a_long_suffix_holds_past_a_long_press(void)
{
    KeyScriptStep steps[CAPACITY];
    CHECK_EQUAL(1, parse("back:long", steps));
    CHECK_EQUAL(KEY_SCRIPT_LONG, steps[0].hold);
    CHECK_EQUAL(KEY_SCRIPT_LONG_MS, key_script_hold_ms(steps[0].hold));
}

static void a_held_suffix_holds_for_seconds(void)
{
    KeyScriptStep steps[CAPACITY];
    CHECK_EQUAL(1, parse("down:held", steps));
    CHECK_EQUAL(KEY_VOLUME_UP, steps[0].key);
    CHECK_EQUAL(KEY_SCRIPT_HELD_MS, key_script_hold_ms(steps[0].hold));
}

static void a_tap_is_shorter_than_a_long_press(void)
{
    CHECK(key_script_hold_ms(KEY_SCRIPT_TAP) < key_script_hold_ms(KEY_SCRIPT_LONG));
    CHECK(key_script_hold_ms(KEY_SCRIPT_LONG) < key_script_hold_ms(KEY_SCRIPT_HELD));
}

static void unknown_names_are_rejected(void)
{
    KeyScriptStep steps[CAPACITY];
    CHECK_EQUAL(0, parse("play,jump", steps));
    CHECK_EQUAL(0, parse("play:short", steps));
    CHECK_EQUAL(0, parse("", steps));
    CHECK_EQUAL(0, parse("play,,back", steps));
    CHECK_EQUAL(0, parse("play,", steps));
    CHECK_EQUAL(0, parse(":long", steps));
    CHECK_EQUAL(0, parse("pla", steps));
    CHECK_EQUAL(0, parse("player", steps));
}

static void a_list_longer_than_the_capacity_is_rejected(void)
{
    KeyScriptStep steps[CAPACITY];
    CHECK_EQUAL(CAPACITY, parse("up,up,up,up,up,up,up,up", steps));
    CHECK_EQUAL(0, parse("up,up,up,up,up,up,up,up,up", steps));
}

static void a_step_ends_at_the_comma_or_the_given_length(void)
{
    CHECK_EQUAL(4, key_script_step_length("play,down", 9));
    CHECK_EQUAL(3, key_script_step_length("play,down", 3));
    CHECK_EQUAL(0, key_script_step_length(",down", 5));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(key_names_map_to_device_keys),
        TEST_CASE(every_key_is_named_as_it_is_parsed),
        TEST_CASE(a_long_suffix_holds_past_a_long_press),
        TEST_CASE(a_held_suffix_holds_for_seconds),
        TEST_CASE(a_tap_is_shorter_than_a_long_press),
        TEST_CASE(unknown_names_are_rejected),
        TEST_CASE(a_list_longer_than_the_capacity_is_rejected),
        TEST_CASE(a_step_ends_at_the_comma_or_the_given_length),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
