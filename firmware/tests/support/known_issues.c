#include "known_issues.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module_guard.h"

static const char *const NAMES[KNOWN_ISSUE_COUNT] = {
    [KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE] = "hold-menu-stale-update",
    [KNOWN_ISSUE_EQUALIZER_STATE_AFTER_RELOAD] = "equalizer-state-after-reload",
    [KNOWN_ISSUE_BLOCKED_PROFILE_CEILING] = "blocked-profile-ceiling",
    [KNOWN_ISSUE_HOME_ART_AFTER_OVERLAY] = "home-art-after-overlay",
    [KNOWN_ISSUE_SLIDE_LEAVING_EVICTED] = "slide-leaving-evicted",
    [KNOWN_ISSUE_DISCOVERY_WORK_AFTER_RESCAN] = "discovery-work-after-rescan",
    [KNOWN_ISSUE_SAFE_START_LABELS_AFTER_RELOAD] = "safe-start-labels-after-reload",
    [KNOWN_ISSUE_PREVIEW_CANCEL_EVICTED] = "preview-cancel-evicted",
};

static int tolerated;

const char *known_issue_name(KnownIssue issue)
{
    return issue < KNOWN_ISSUE_COUNT ? NAMES[issue] : "unknown";
}

static bool listed(const char *list, const char *name)
{
    size_t length = strlen(name);
    for (const char *at = list; at != NULL && *at != '\0';) {
        const char *end = strchr(at, ',');
        size_t span = end != NULL ? (size_t)(end - at) : strlen(at);
        if ((span == length && strncmp(at, name, length) == 0) ||
            (span == 3 && strncmp(at, "all", 3) == 0)) {
            return true;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    return false;
}

bool known_issue_open(KnownIssue issue)
{
    return !listed(getenv("ECHO_KNOWN_ISSUES"), known_issue_name(issue));
}

static void count_call(const char *report, const void *function)
{
    (void)report;
    (void)function;
    tolerated++;
}

void known_issue_tolerate_calls(KnownIssue issue)
{
    tolerated = 0;
    if (known_issue_open(issue)) {
        module_guard_set_trap(count_call);
    }
}

int known_issue_tolerated(void)
{
    return tolerated;
}

void known_issue_end(void)
{
    module_guard_set_trap(NULL);
}
