#pragma once

#include <stdbool.h>

typedef enum KnownIssue {
    KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE,
    KNOWN_ISSUE_EQUALIZER_STATE_AFTER_RELOAD,
    KNOWN_ISSUE_BLOCKED_PROFILE_CEILING,
    KNOWN_ISSUE_HOME_ART_AFTER_OVERLAY,
    KNOWN_ISSUE_SLIDE_LEAVING_EVICTED,
    KNOWN_ISSUE_DISCOVERY_WORK_AFTER_RESCAN,
    KNOWN_ISSUE_SAFE_START_LABELS_AFTER_RELOAD,
    KNOWN_ISSUE_PREVIEW_CANCEL_EVICTED,
    KNOWN_ISSUE_COUNT
} KnownIssue;

const char *known_issue_name(KnownIssue issue);
bool known_issue_open(KnownIssue issue);
void known_issue_tolerate_calls(KnownIssue issue);
int known_issue_tolerated(void);
void known_issue_end(void);
