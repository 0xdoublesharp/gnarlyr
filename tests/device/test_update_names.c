#include <string.h>

#include "check.h"
#include "device/update_names.h"

static void check_name(UpdateBoard board, uint32_t index, const char *expected)
{
    char name[UPDATE_NAME_CAPACITY];
    CHECK(update_name_at(board, index, name));
    CHECK(strcmp(expected, name) == 0);
}

static void the_mini_scans_hifiec97_down_to_hifiec00(void)
{
    CHECK_EQUAL(98, update_name_count(UPDATE_BOARD_MINI));
    check_name(UPDATE_BOARD_MINI, 0, "/HIFIEC97.IMG");
    check_name(UPDATE_BOARD_MINI, 1, "/HIFIEC96.IMG");
    check_name(UPDATE_BOARD_MINI, 7, "/HIFIEC90.IMG");
    check_name(UPDATE_BOARD_MINI, 97, "/HIFIEC00.IMG");
}

static void the_echo_scans_echoecho_first_then_echov498_down_to_echov100(void)
{
    CHECK_EQUAL(400, update_name_count(UPDATE_BOARD_ECHO));
    check_name(UPDATE_BOARD_ECHO, 0, "/ECHOECHO.IMG");
    check_name(UPDATE_BOARD_ECHO, 1, "/ECHOV498.IMG");
    check_name(UPDATE_BOARD_ECHO, 2, "/ECHOV497.IMG");
    check_name(UPDATE_BOARD_ECHO, 399, "/ECHOV100.IMG");
}

static void the_highest_numbered_echo_name_wins_over_a_lower_one(void)
{
    char name[UPDATE_NAME_CAPACITY];
    uint32_t first_498 = 0;
    uint32_t first_100 = 0;
    for (uint32_t index = 0; index < update_name_count(UPDATE_BOARD_ECHO); index++) {
        CHECK(update_name_at(UPDATE_BOARD_ECHO, index, name));
        first_498 = strcmp(name, "/ECHOV498.IMG") == 0 ? index : first_498;
        first_100 = strcmp(name, "/ECHOV100.IMG") == 0 ? index : first_100;
    }
    CHECK(first_498 < first_100);
}

static void an_index_past_the_end_gives_no_name(void)
{
    char name[UPDATE_NAME_CAPACITY];
    CHECK(!update_name_at(UPDATE_BOARD_MINI, 98, name));
    CHECK(!update_name_at(UPDATE_BOARD_ECHO, 400, name));
}

static void every_name_is_an_eight_dot_three_path_at_the_root(void)
{
    UpdateBoard boards[] = {UPDATE_BOARD_MINI, UPDATE_BOARD_ECHO};
    for (int board = 0; board < 2; board++) {
        for (uint32_t index = 0; index < update_name_count(boards[board]); index++) {
            char name[UPDATE_NAME_CAPACITY];
            CHECK(update_name_at(boards[board], index, name));
            CHECK_EQUAL(13, strlen(name));
            CHECK_EQUAL('/', name[0]);
            CHECK(strcmp(name + 9, ".IMG") == 0);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_mini_scans_hifiec97_down_to_hifiec00),
        TEST_CASE(the_echo_scans_echoecho_first_then_echov498_down_to_echov100),
        TEST_CASE(the_highest_numbered_echo_name_wins_over_a_lower_one),
        TEST_CASE(an_index_past_the_end_gives_no_name),
        TEST_CASE(every_name_is_an_eight_dot_three_path_at_the_root),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
