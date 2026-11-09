#include "check.h"
#include "settings/rocker.h"

static void only_the_first_portrait_swaps_the_rocker(void)
{
    CHECK(rocker_swapped_in(ORIENTATION_PORTRAIT));
    CHECK(!rocker_swapped_in(ORIENTATION_PORTRAIT_FLIPPED));
    CHECK(!rocker_swapped_in(ORIENTATION_LANDSCAPE));
}

static void only_the_flipped_portrait_swaps_the_volume(void)
{
    CHECK(rocker_volume_swapped_in(ORIENTATION_PORTRAIT_FLIPPED));
    CHECK(!rocker_volume_swapped_in(ORIENTATION_PORTRAIT));
    CHECK(!rocker_volume_swapped_in(ORIENTATION_LANDSCAPE));
}

static void a_swapped_rocker_exchanges_the_volume_keys(void)
{
    CHECK_EQUAL(KEY_VOLUME_DOWN, rocker_key(KEY_VOLUME_UP, true));
    CHECK_EQUAL(KEY_VOLUME_UP, rocker_key(KEY_VOLUME_DOWN, true));
}

static void an_unswapped_rocker_keeps_the_volume_keys(void)
{
    CHECK_EQUAL(KEY_VOLUME_UP, rocker_key(KEY_VOLUME_UP, false));
    CHECK_EQUAL(KEY_VOLUME_DOWN, rocker_key(KEY_VOLUME_DOWN, false));
}

static void the_other_keys_are_never_swapped(void)
{
    static const Key others[] = {KEY_PLAY, KEY_MENU, KEY_BACK, KEY_POWER};
    for (int index = 0; index < 4; index++) {
        CHECK_EQUAL(others[index], rocker_key(others[index], true));
        CHECK_EQUAL(others[index], rocker_key(others[index], false));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(only_the_first_portrait_swaps_the_rocker),
        TEST_CASE(only_the_flipped_portrait_swaps_the_volume),
        TEST_CASE(a_swapped_rocker_exchanges_the_volume_keys),
        TEST_CASE(an_unswapped_rocker_keeps_the_volume_keys),
        TEST_CASE(the_other_keys_are_never_swapped),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
