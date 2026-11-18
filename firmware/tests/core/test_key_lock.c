#include "check.h"
#include "core/key_lock.h"

static KeyLock hold_combo(KeyLock lock, uint32_t start_ms)
{
    key_lock_transition(&lock, KEY_PLAY, true, start_ms);
    key_lock_transition(&lock, KEY_POWER, true, start_ms + 50);
    key_lock_update(&lock, start_ms + 50 + KEY_LOCK_HOLD_MS);
    key_lock_transition(&lock, KEY_PLAY, false, start_ms + 1200);
    key_lock_transition(&lock, KEY_POWER, false, start_ms + 1210);
    return lock;
}

static void holding_play_and_power_locks_and_unlocks(void)
{
    KeyLock lock = hold_combo((KeyLock){0}, 0);
    CHECK(lock.locked);
    lock = hold_combo(lock, 5000);
    CHECK(!lock.locked);
}

static void the_combination_starts_on_the_second_key_and_swallows_both(void)
{
    KeyLock lock = {0};
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, true, 0));
    CHECK_EQUAL(KEY_LOCK_COMBO_STARTED, key_lock_transition(&lock, KEY_PLAY, true, 30));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_PLAY, false, 200));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_POWER, false, 220));
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, true, 400));
}

static void a_short_combination_changes_nothing(void)
{
    KeyLock lock = {0};
    key_lock_transition(&lock, KEY_PLAY, true, 0);
    key_lock_transition(&lock, KEY_POWER, true, 10);
    CHECK(!key_lock_update(&lock, 10 + KEY_LOCK_HOLD_MS - 1));
    key_lock_transition(&lock, KEY_POWER, false, 500);
    CHECK(!key_lock_update(&lock, 2000));
    CHECK(!lock.locked);
}

static void a_long_combination_toggles_only_once(void)
{
    KeyLock lock = {0};
    key_lock_transition(&lock, KEY_PLAY, true, 0);
    key_lock_transition(&lock, KEY_POWER, true, 10);
    CHECK(key_lock_update(&lock, 1100));
    CHECK(!key_lock_update(&lock, 3000));
    CHECK(lock.locked);
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_PLAY, false, 3100));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_PLAY, true, 3200));
    CHECK(!key_lock_update(&lock, 5000));
    CHECK(lock.locked);
}

static void locked_keys_are_refused_on_press_and_swallowed_on_release(void)
{
    KeyLock lock = hold_combo((KeyLock){0}, 0);
    CHECK_EQUAL(KEY_LOCK_REFUSED, key_lock_transition(&lock, KEY_MENU, true, 3000));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_MENU, false, 3100));
    CHECK_EQUAL(KEY_LOCK_REFUSED, key_lock_transition(&lock, KEY_VOLUME_UP, true, 3200));
    CHECK_EQUAL(KEY_LOCK_REFUSED, key_lock_transition(&lock, KEY_PLAY, true, 3300));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_PLAY, false, 3400));
}

static void power_keeps_working_while_locked(void)
{
    KeyLock lock = hold_combo((KeyLock){0}, 0);
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, true, 3000));
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, false, 3100));
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, true, 4000));
    CHECK_EQUAL(KEY_LOCK_COMBO_STARTED, key_lock_transition(&lock, KEY_PLAY, true, 4050));
    CHECK(key_lock_update(&lock, 4050 + KEY_LOCK_HOLD_MS));
    CHECK(!lock.locked);
}

static void a_hold_crossing_the_clock_wrap_toggles_at_the_deadline(void)
{
    KeyLock lock = {0};
    uint32_t start = UINT32_MAX - 100;
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_PLAY, true, start));
    CHECK_EQUAL(KEY_LOCK_COMBO_STARTED, key_lock_transition(&lock, KEY_POWER, true, start));
    CHECK(!key_lock_update(&lock, start + KEY_LOCK_HOLD_MS - 1));
    CHECK(!lock.locked);
    CHECK(key_lock_update(&lock, start + KEY_LOCK_HOLD_MS));
    CHECK(lock.locked);
    CHECK(!key_lock_update(&lock, start + KEY_LOCK_HOLD_MS + 1));
}

static void releasing_one_combo_key_requires_both_keys_to_be_released(void)
{
    KeyLock lock = {0};
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_PLAY, true, 0));
    CHECK_EQUAL(KEY_LOCK_COMBO_STARTED, key_lock_transition(&lock, KEY_POWER, true, 1));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_PLAY, false, 2));
    CHECK(!key_lock_update(&lock, KEY_LOCK_HOLD_MS + 3));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_POWER, false, 5));
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_PLAY, true, 6));
    CHECK_EQUAL(KEY_LOCK_COMBO_STARTED, key_lock_transition(&lock, KEY_POWER, true, 7));
}

static void a_released_and_repressed_key_must_be_held_for_the_full_interval(void)
{
    KeyLock lock = {0};
    key_lock_transition(&lock, KEY_PLAY, true, 0);
    key_lock_transition(&lock, KEY_POWER, true, 0);
    key_lock_transition(&lock, KEY_PLAY, false, 500);
    key_lock_transition(&lock, KEY_PLAY, true, 900);
    CHECK(!key_lock_update(&lock, KEY_LOCK_HOLD_MS));
    CHECK(!lock.locked);
    CHECK(!key_lock_update(&lock, 900 + KEY_LOCK_HOLD_MS - 1));
    CHECK(key_lock_update(&lock, 900 + KEY_LOCK_HOLD_MS));
    CHECK(lock.locked);
}

static void power_rocker_is_available_while_locked(void)
{
    KeyLock lock = {.locked = true};
    CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, true, 0));
    CHECK_EQUAL(KEY_LOCK_SHORTCUT, key_lock_transition(&lock, KEY_VOLUME_UP, true, 20));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_VOLUME_UP, false, 30));
    CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, KEY_POWER, false, 40));
    CHECK(lock.locked);
}

static void shortcuts_fire_once_and_drain_both_release_orders(void)
{
    const Key keys[] = {KEY_VOLUME_UP, KEY_VOLUME_DOWN, KEY_MENU};
    const KeyLockVerdict verdicts[] = {KEY_LOCK_SHORTCUT, KEY_LOCK_SHORTCUT, KEY_LOCK_SHORTCUT};
    for (int index = 0; index < 3; index++) {
        for (int order = 0; order < 2; order++) {
            KeyLock lock = {0};
            key_lock_transition(&lock, KEY_POWER, true, 0);
            CHECK_EQUAL(verdicts[index], key_lock_transition(&lock, keys[index], true, 10));
            CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, keys[index], true, 20));
            CHECK(!key_lock_update(&lock, 5000));
            Key first = order == 0 ? KEY_POWER : keys[index];
            Key second = order == 0 ? keys[index] : KEY_POWER;
            CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, first, false, 5010));
            CHECK_EQUAL(KEY_LOCK_SWALLOW, key_lock_transition(&lock, second, false, 5020));
            CHECK_EQUAL(KEY_LOCK_PASS, key_lock_transition(&lock, KEY_POWER, true, 5030));
            CHECK(!lock.locked);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(shortcuts_fire_once_and_drain_both_release_orders),
        TEST_CASE(power_rocker_is_available_while_locked),
        TEST_CASE(holding_play_and_power_locks_and_unlocks),
        TEST_CASE(the_combination_starts_on_the_second_key_and_swallows_both),
        TEST_CASE(a_short_combination_changes_nothing),
        TEST_CASE(a_long_combination_toggles_only_once),
        TEST_CASE(locked_keys_are_refused_on_press_and_swallowed_on_release),
        TEST_CASE(power_keeps_working_while_locked),
        TEST_CASE(a_hold_crossing_the_clock_wrap_toggles_at_the_deadline),
        TEST_CASE(releasing_one_combo_key_requires_both_keys_to_be_released),
        TEST_CASE(a_released_and_repressed_key_must_be_held_for_the_full_interval),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
