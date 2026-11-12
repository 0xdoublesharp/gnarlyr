#include <string.h>

#include "check.h"
#include "device/update_trial.h"

static UpdateTrial round_trip(UpdateTrial trial)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    update_trial_encode(trial, bytes);
    return update_trial_decode(bytes);
}

static void a_record_survives_encoding(void)
{
    UpdateTrial trial = round_trip((UpdateTrial){UPDATE_TRIAL_RUNNING, 2});
    CHECK_EQUAL(UPDATE_TRIAL_RUNNING, trial.state);
    CHECK_EQUAL(2, trial.attempts);
}

static void zeros_and_damaged_records_mean_no_trial(void)
{
    uint8_t bytes[UPDATE_TRIAL_BYTES] = {0};
    CHECK_EQUAL(UPDATE_TRIAL_NONE, update_trial_decode(bytes).state);
    update_trial_encode((UpdateTrial){UPDATE_TRIAL_RUNNING, 1}, bytes);
    bytes[5] = 2;
    CHECK_EQUAL(UPDATE_TRIAL_NONE, update_trial_decode(bytes).state);
    memset(bytes, 0xFF, sizeof bytes);
    CHECK_EQUAL(UPDATE_TRIAL_NONE, update_trial_decode(bytes).state);
}

static void each_boot_of_a_trial_is_charged_until_the_limit(void)
{
    UpdateTrial trial = {UPDATE_TRIAL_RUNNING, 0};
    for (int boot = 0; boot < UPDATE_TRIAL_LIMIT; boot++) {
        CHECK_EQUAL(UPDATE_TRIAL_STEP_CHARGED, update_trial_boot(&trial));
    }
    CHECK_EQUAL(UPDATE_TRIAL_LIMIT, trial.attempts);
    CHECK_EQUAL(UPDATE_TRIAL_STEP_ROLL_BACK, update_trial_boot(&trial));
    CHECK_EQUAL(UPDATE_TRIAL_LIMIT, trial.attempts);
}

static void a_settled_boot_is_refunded(void)
{
    UpdateTrial trial = {UPDATE_TRIAL_RUNNING, 0};
    for (int boot = 0; boot < 2 * UPDATE_TRIAL_LIMIT; boot++) {
        CHECK_EQUAL(UPDATE_TRIAL_STEP_CHARGED, update_trial_boot(&trial));
        CHECK(update_trial_settle(&trial));
    }
    CHECK_EQUAL(0, trial.attempts);
    CHECK(!update_trial_settle(&trial));
}

static void a_usb_session_is_charged_on_top_of_the_boot(void)
{
    UpdateTrial trial = {UPDATE_TRIAL_RUNNING, 0};
    update_trial_boot(&trial);
    CHECK(update_trial_charge(&trial));
    CHECK_EQUAL(2, trial.attempts);
    update_trial_boot(&trial);
    CHECK(update_trial_charge(&trial));
    CHECK_EQUAL(UPDATE_TRIAL_STEP_ROLL_BACK, update_trial_boot(&trial));
}

static void a_rollback_is_reported_once(void)
{
    UpdateTrial trial = {UPDATE_TRIAL_ROLLED_BACK, 3};
    CHECK_EQUAL(UPDATE_TRIAL_STEP_REPORT, update_trial_boot(&trial));
    CHECK_EQUAL(UPDATE_TRIAL_NONE, trial.state);
    CHECK_EQUAL(UPDATE_TRIAL_STEP_NOTHING, update_trial_boot(&trial));
}

static void without_a_running_trial_nothing_is_charged(void)
{
    UpdateTrialState states[] = {UPDATE_TRIAL_NONE, UPDATE_TRIAL_PROMOTING};
    for (size_t index = 0; index < sizeof states / sizeof states[0]; index++) {
        UpdateTrial trial = {states[index], 7};
        CHECK_EQUAL(UPDATE_TRIAL_STEP_NOTHING, update_trial_boot(&trial));
        CHECK(!update_trial_charge(&trial));
        CHECK(!update_trial_settle(&trial));
        CHECK_EQUAL(7, trial.attempts);
    }
}

static void the_charge_stops_at_its_largest_value(void)
{
    UpdateTrial trial = {UPDATE_TRIAL_RUNNING, 0xFF};
    CHECK(!update_trial_charge(&trial));
    CHECK_EQUAL(0xFF, trial.attempts);
}

static void the_started_record_constant_is_running_with_no_attempts(void)
{
    static const uint8_t STARTED[UPDATE_TRIAL_BYTES] = UPDATE_TRIAL_STARTED;
    uint8_t bytes[UPDATE_TRIAL_BYTES];
    update_trial_encode((UpdateTrial){UPDATE_TRIAL_RUNNING, 0}, bytes);
    CHECK(memcmp(STARTED, bytes, sizeof bytes) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_record_survives_encoding),
        TEST_CASE(zeros_and_damaged_records_mean_no_trial),
        TEST_CASE(each_boot_of_a_trial_is_charged_until_the_limit),
        TEST_CASE(a_settled_boot_is_refunded),
        TEST_CASE(a_usb_session_is_charged_on_top_of_the_boot),
        TEST_CASE(a_rollback_is_reported_once),
        TEST_CASE(without_a_running_trial_nothing_is_charged),
        TEST_CASE(the_charge_stops_at_its_largest_value),
        TEST_CASE(the_started_record_constant_is_running_with_no_attempts),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
