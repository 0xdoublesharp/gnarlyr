#include "check.h"
#include "device/key_ladder.h"
#include "device/key_scanner.h"

static const uint8_t PLAY = 1u << KEY_PLAY;
static const uint8_t POWER = 1u << KEY_POWER;

static void settle(KeyScanner *scanner, uint8_t mask, uint32_t time_ms)
{
    for (uint32_t index = 0; index < KEY_SCANNER_PRESS_SAMPLES; index++) {
        key_scanner_sample(scanner, mask, time_ms + index * 5);
    }
}

static void check_next(KeyScanner *scanner, Key key, bool pressed, uint32_t time_ms)
{
    HalKeyTransition transition = {0};
    CHECK(key_scanner_next(scanner, &transition));
    CHECK_EQUAL(key, transition.key);
    CHECK_EQUAL(pressed, transition.pressed);
    CHECK_EQUAL(time_ms, transition.time_ms);
}

static void check_empty(KeyScanner *scanner)
{
    HalKeyTransition transition = {0};
    CHECK(!key_scanner_next(scanner, &transition));
}

static KeyScanner armed_scanner(void)
{
    KeyScanner scanner;
    key_scanner_init(&scanner);
    settle(&scanner, 0, 0);
    return scanner;
}

static void a_single_sample_is_not_a_press(void)
{
    KeyScanner scanner = armed_scanner();
    key_scanner_sample(&scanner, PLAY, 100);
    check_empty(&scanner);
}

static void matching_samples_are_a_press_at_the_last_needed_sample(void)
{
    KeyScanner scanner = armed_scanner();
    settle(&scanner, PLAY, 100);
    check_next(&scanner, KEY_PLAY, true, 115);
    check_empty(&scanner);
}

static void a_bounce_between_samples_is_ignored(void)
{
    KeyScanner scanner = armed_scanner();
    key_scanner_sample(&scanner, PLAY, 100);
    key_scanner_sample(&scanner, 0, 105);
    key_scanner_sample(&scanner, PLAY, 110);
    check_empty(&scanner);
}

static void a_release_follows_a_press(void)
{
    KeyScanner scanner = armed_scanner();
    settle(&scanner, PLAY, 100);
    settle(&scanner, 0, 200);
    check_next(&scanner, KEY_PLAY, true, 115);
    check_next(&scanner, KEY_PLAY, false, 205);
}

static void a_held_key_reports_one_press(void)
{
    KeyScanner scanner = armed_scanner();
    settle(&scanner, PLAY, 100);
    settle(&scanner, PLAY, 200);
    check_next(&scanner, KEY_PLAY, true, 115);
    check_empty(&scanner);
}

static void switching_ladder_keys_releases_the_old_key_first(void)
{
    KeyScanner scanner = armed_scanner();
    settle(&scanner, PLAY, 100);
    settle(&scanner, 1u << KEY_MENU, 200);
    check_next(&scanner, KEY_PLAY, true, 115);
    check_next(&scanner, KEY_PLAY, false, 215);
    check_next(&scanner, KEY_MENU, true, 215);
}

static void power_held_from_switch_on_is_ignored_until_released(void)
{
    KeyScanner scanner;
    key_scanner_init(&scanner);
    settle(&scanner, POWER, 0);
    settle(&scanner, POWER, 900);
    check_empty(&scanner);
    settle(&scanner, 0, 1000);
    check_empty(&scanner);
    settle(&scanner, POWER, 2000);
    check_next(&scanner, KEY_POWER, true, 2015);
}

static void ladder_keys_work_while_the_switch_on_power_press_is_held(void)
{
    KeyScanner scanner;
    key_scanner_init(&scanner);
    settle(&scanner, POWER | PLAY, 0);
    check_next(&scanner, KEY_PLAY, true, 15);
    check_empty(&scanner);
}

static void a_full_queue_drops_new_transitions(void)
{
    KeyScanner scanner = armed_scanner();
    for (uint32_t index = 0; index < KEY_SCANNER_QUEUE_CAPACITY; index++) {
        settle(&scanner, (index % 2 == 0) ? PLAY : 0, 100 + index * 10);
    }
    settle(&scanner, 1u << KEY_MENU, 1000);
    for (uint32_t index = 0; index < KEY_SCANNER_QUEUE_CAPACITY; index++) {
        HalKeyTransition transition = {0};
        CHECK(key_scanner_next(&scanner, &transition));
        CHECK_EQUAL(KEY_PLAY, transition.key);
    }
    check_empty(&scanner);
}

static void a_reading_passing_through_another_key_on_release_is_not_a_press(void)
{
    KeyScanner scanner = armed_scanner();
    const uint8_t volume = 1u << KEY_VOLUME_UP;
    settle(&scanner, volume, 100);
    key_scanner_sample(&scanner, PLAY, 400);
    key_scanner_sample(&scanner, 0, 405);
    key_scanner_sample(&scanner, 0, 410);
    check_next(&scanner, KEY_VOLUME_UP, true, 115);
    check_next(&scanner, KEY_VOLUME_UP, false, 410);
    check_empty(&scanner);
}

static void feed_reading(KeyScanner *scanner, uint16_t reading, uint32_t time_ms)
{
    key_scanner_sample(scanner, key_ladder_held_mask(reading, scanner->stable), time_ms);
}

static void releasing_volume_down_through_the_other_bands_is_not_a_volume_up(void)
{
    KeyScanner scanner;
    key_scanner_init(&scanner);
    for (uint32_t index = 0; index < KEY_SCANNER_PRESS_SAMPLES; index++) {
        feed_reading(&scanner, 30, 100 + index * 5);
    }
    check_next(&scanner, KEY_VOLUME_DOWN, true, 115);
    feed_reading(&scanner, 120, 120);
    feed_reading(&scanner, 190, 125);
    feed_reading(&scanner, 1023, 130);
    feed_reading(&scanner, 1023, 135);
    check_next(&scanner, KEY_VOLUME_DOWN, false, 135);
    check_empty(&scanner);
}

static void a_held_key_survives_a_reading_between_bands(void)
{
    KeyScanner scanner;
    key_scanner_init(&scanner);
    for (uint32_t index = 0; index < KEY_SCANNER_PRESS_SAMPLES; index++) {
        feed_reading(&scanner, 205, 100 + index * 5);
    }
    check_next(&scanner, KEY_PLAY, true, 115);
    feed_reading(&scanner, 195, 120);
    feed_reading(&scanner, 195, 125);
    feed_reading(&scanner, 205, 130);
    check_empty(&scanner);
}

static void a_short_pass_through_another_key_is_not_a_press(void)
{
    KeyScanner scanner = armed_scanner();
    key_scanner_sample(&scanner, PLAY, 100);
    key_scanner_sample(&scanner, PLAY, 105);
    key_scanner_sample(&scanner, PLAY, 110);
    key_scanner_sample(&scanner, 0, 115);
    key_scanner_sample(&scanner, 0, 120);
    check_empty(&scanner);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_short_pass_through_another_key_is_not_a_press),
        TEST_CASE(releasing_volume_down_through_the_other_bands_is_not_a_volume_up),
        TEST_CASE(a_held_key_survives_a_reading_between_bands),
        TEST_CASE(a_single_sample_is_not_a_press),
        TEST_CASE(matching_samples_are_a_press_at_the_last_needed_sample),
        TEST_CASE(a_bounce_between_samples_is_ignored),
        TEST_CASE(a_reading_passing_through_another_key_on_release_is_not_a_press),
        TEST_CASE(a_release_follows_a_press),
        TEST_CASE(a_held_key_reports_one_press),
        TEST_CASE(switching_ladder_keys_releases_the_old_key_first),
        TEST_CASE(power_held_from_switch_on_is_ignored_until_released),
        TEST_CASE(ladder_keys_work_while_the_switch_on_power_press_is_held),
        TEST_CASE(a_full_queue_drops_new_transitions),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
