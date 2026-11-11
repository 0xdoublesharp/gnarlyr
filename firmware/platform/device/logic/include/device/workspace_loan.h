#pragma once

#include <stdbool.h>

typedef struct WorkspaceActivity {
    bool playing;
    bool stream_open;
    bool output_up;
    bool core1_busy;
    bool tone;
} WorkspaceActivity;

typedef struct WorkspaceLoan {
    bool lent;
} WorkspaceLoan;

bool workspace_loan_take(WorkspaceLoan *loan, WorkspaceActivity activity);
void workspace_loan_return(WorkspaceLoan *loan);
bool workspace_loan_claimable(const WorkspaceLoan *loan);
