#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include "known_issues.h"

#include <dlfcn.h>
#include <execinfo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module_guard.h"

static const char *const NAMES[KNOWN_ISSUE_COUNT] = {
    [KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE] = "hold-menu-stale-update",
    [KNOWN_ISSUE_EQUALIZER_STATE_AFTER_RELOAD] = "equalizer-state-after-reload",
    [KNOWN_ISSUE_HOME_ART_AFTER_OVERLAY] = "home-art-after-overlay",
    [KNOWN_ISSUE_SLIDE_LEAVING_EVICTED] = "slide-leaving-evicted",
    [KNOWN_ISSUE_DISCOVERY_WORK_AFTER_RESCAN] = "discovery-work-after-rescan",
    [KNOWN_ISSUE_SAFE_START_LABELS_AFTER_RELOAD] = "safe-start-labels-after-reload",
    [KNOWN_ISSUE_PREVIEW_CANCEL_EVICTED] = "preview-cancel-evicted",
    [KNOWN_ISSUE_PARAMETRIC_BANK_AFTER_RELOAD] = "parametric-bank-after-reload",
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

static bool on_stack(const char *name)
{
    void *frames[64];
    int count = backtrace(frames, 64);
    for (int index = 0; index < count; index++) {
        Dl_info info;
        if (dladdr(frames[index], &info) != 0 && info.dli_sname != NULL &&
            strcmp(info.dli_sname, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool overlay_evicted(const char *report)
{
    return strstr(report, "the rescan overlay") != NULL ||
           strstr(report, "the USB overlay") != NULL;
}

static bool shared_art(const char *report)
{
    return strstr(report, "views+settings+library+listening+equalizer+playing+stage") != NULL;
}

bool known_issue_covers_call(const char *report)
{
    bool overlay = overlay_evicted(report);
    return (!overlay && known_issue_open(KNOWN_ISSUE_SLIDE_LEAVING_EVICTED) &&
            on_stack("strip_renderer_render")) ||
           (overlay && shared_art(report) &&
            known_issue_open(KNOWN_ISSUE_HOME_ART_AFTER_OVERLAY)) ||
           (known_issue_open(KNOWN_ISSUE_PREVIEW_CANCEL_EVICTED) &&
            (on_stack("screen_modules_cancel_previews") || on_stack("screen_modules_use"))) ||
           (strstr(report, "for a Hold Menu shortcut") != NULL &&
            known_issue_open(KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE));
}

static jmp_buf *fuzz_end;

static void fuzz_trap(const char *report, const void *function)
{
    (void)function;
    if (fuzz_end != NULL && known_issue_covers_call(report)) {
        longjmp(*fuzz_end, 1);
    }
    module_guard_abort(report);
}

void known_issue_fuzz_guard(jmp_buf *end)
{
    fuzz_end = end;
    module_guard_set_trap(fuzz_trap);
}
