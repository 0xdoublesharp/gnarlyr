#include <string.h>

#include "check.h"
#include "device/update_names.h"

static void check_name(uint32_t index, const char *expected)
{
    char name[UPDATE_NAME_CAPACITY];
    CHECK(update_name_at(index, name));
    CHECK(strcmp(expected, name) == 0);
}

static void the_player_scans_echoecho_first_then_echov498_down_to_echov100(void)
{
    CHECK_EQUAL(400, update_name_count());
    check_name(0, "/ECHOECHO.IMG");
    check_name(1, "/ECHOV498.IMG");
    check_name(2, "/ECHOV497.IMG");
    check_name(399, "/ECHOV100.IMG");
}

static void the_highest_numbered_name_wins_over_a_lower_one(void)
{
    char name[UPDATE_NAME_CAPACITY];
    uint32_t first_498 = 0;
    uint32_t first_100 = 0;
    for (uint32_t index = 0; index < update_name_count(); index++) {
        CHECK(update_name_at(index, name));
        first_498 = strcmp(name, "/ECHOV498.IMG") == 0 ? index : first_498;
        first_100 = strcmp(name, "/ECHOV100.IMG") == 0 ? index : first_100;
    }
    CHECK(first_498 < first_100);
}

static void an_index_past_the_end_gives_no_name(void)
{
    char name[UPDATE_NAME_CAPACITY];
    CHECK(!update_name_at(400, name));
}

static void every_name_is_an_eight_dot_three_path_at_the_root(void)
{
    for (uint32_t index = 0; index < update_name_count(); index++) {
        char name[UPDATE_NAME_CAPACITY];
        CHECK(update_name_at(index, name));
        CHECK_EQUAL(13, strlen(name));
        CHECK_EQUAL('/', name[0]);
        CHECK(strcmp(name + 9, ".IMG") == 0);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_player_scans_echoecho_first_then_echov498_down_to_echov100),
        TEST_CASE(the_highest_numbered_name_wins_over_a_lower_one),
        TEST_CASE(an_index_past_the_end_gives_no_name),
        TEST_CASE(every_name_is_an_eight_dot_three_path_at_the_root),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
