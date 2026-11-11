#include "check.h"
#include "device/workspace_loan.h"

static void a_released_workspace_is_lent_once(void)
{
    WorkspaceLoan loan = {0};
    CHECK(workspace_loan_take(&loan, (WorkspaceActivity){0}));
    CHECK(!workspace_loan_claimable(&loan));
    CHECK(!workspace_loan_take(&loan, (WorkspaceActivity){0}));
}

static void any_audio_activity_refuses_the_loan(void)
{
    static const WorkspaceActivity BUSY[] = {
        {.playing = true},    {.stream_open = true}, {.output_up = true},
        {.core1_busy = true}, {.tone = true},
    };
    for (unsigned at = 0; at < sizeof BUSY / sizeof BUSY[0]; at++) {
        WorkspaceLoan loan = {0};
        CHECK(!workspace_loan_take(&loan, BUSY[at]));
        CHECK(workspace_loan_claimable(&loan));
    }
}

static void a_returned_workspace_can_be_claimed_again(void)
{
    WorkspaceLoan loan = {0};
    CHECK(workspace_loan_take(&loan, (WorkspaceActivity){0}));
    workspace_loan_return(&loan);
    CHECK(workspace_loan_claimable(&loan));
    CHECK(workspace_loan_take(&loan, (WorkspaceActivity){0}));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_released_workspace_is_lent_once),
        TEST_CASE(any_audio_activity_refuses_the_loan),
        TEST_CASE(a_returned_workspace_can_be_claimed_again),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
