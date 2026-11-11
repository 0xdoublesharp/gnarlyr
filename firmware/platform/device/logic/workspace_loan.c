#include "device/workspace_loan.h"

static bool released(WorkspaceActivity activity)
{
    return !activity.playing && !activity.stream_open && !activity.output_up &&
           !activity.core1_busy && !activity.tone;
}

bool workspace_loan_take(WorkspaceLoan *loan, WorkspaceActivity activity)
{
    if (loan->lent || !released(activity)) {
        return false;
    }
    loan->lent = true;
    return true;
}

void workspace_loan_return(WorkspaceLoan *loan)
{
    loan->lent = false;
}

bool workspace_loan_claimable(const WorkspaceLoan *loan)
{
    return !loan->lent;
}
