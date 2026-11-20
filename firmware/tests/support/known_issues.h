#pragma once

#include <stdbool.h>

typedef enum KnownIssue {
    KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE,
    KNOWN_ISSUE_COUNT
} KnownIssue;

const char *known_issue_name(KnownIssue issue);
bool known_issue_open(KnownIssue issue);
void known_issue_tolerate_calls(KnownIssue issue);
int known_issue_tolerated(void);
void known_issue_end(void);
