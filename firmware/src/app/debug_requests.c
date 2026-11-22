#include "debug_requests.h"

#include <stddef.h>

#include "app_screens.h"
#include "core/power_policy.h"
#include "debug_runner_state.h"
#include "debug_screenshot.h"
#include "debug_steps.h"
#include "modules/modules.h"
#include "player.h"
#include "screen_modules.h"
#include "settings_choose.h"
#include "settings_store.h"

static void finish(bool succeeded)
{
    if (debug_runner.phase != DEBUG_PHASE_REQUESTED) {
        return;
    }
    if (succeeded && debug_runner.requested == DEBUG_REQUEST_SCREENSHOT) {
        DebugScriptRun *run = &debug_runner.run;
        run->screenshot_lines[run->screenshot_count++] = debug_runner.line;
    }
    debug_runner_set_status(succeeded ? DEBUG_LINE_OK : DEBUG_LINE_FAILED);
}

static void start(const DebugAppHooks *app, uint32_t now_ms)
{
    if (*app->charging) {
        app->stop_charging(now_ms);
    }
    power_policy_wake(app->power, now_ms);
}

static void set_screen(const DebugAppHooks *app, bool on, uint32_t now_ms)
{
    if (on) {
        power_policy_wake(app->power, now_ms);
    } else {
        power_policy_rest(app->power);
    }
}

static bool choose(const DebugAppHooks *app, const DebugRequest *request, uint32_t now_ms)
{
    const SettingsModuleEntry *module =
        (const SettingsModuleEntry *)screen_modules_use(app->screens, SCREEN_MODULE_SETTINGS);
    SettingsChooseTarget target = {&app->screens->modules.settings.settings,
                                   module != NULL ? module->choose_tables : NULL};
    app->settings->values_failed = false;
    bool chosen =
        target.tables != NULL && settings_choose(&target, request->argument, request->value);
    app->settle(now_ms);
    return chosen && !app->settings->values_failed;
}

static bool screenshot(const DebugAppHooks *app, const DebugRequest *request)
{
    DebugScreenshot shot = {.name = request->argument, .slot = request->slot};
    app->frame(&shot);
    return debug_screenshot_save(&shot);
}

static bool serve(const DebugAppHooks *app, const DebugRequest *request, uint32_t now_ms)
{
    switch (request->kind) {
    case DEBUG_REQUEST_SCREEN:
        set_screen(app, request->slot != 0, now_ms);
        break;
    case DEBUG_REQUEST_SET:
        return choose(app, request, now_ms);
    case DEBUG_REQUEST_SCREENSHOT:
        return screenshot(app, request);
    case DEBUG_REQUEST_STANDBY:
        app->player->standby_held = request->slot == 0;
        break;
    case DEBUG_REQUEST_RESCAN:
        *app->rescan_manual |= request->slot == 0;
        *app->rescan_requested |= request->slot != 0;
        break;
    default:
        app->open(request->slot, now_ms);
        break;
    }
    return true;
}

void debug_requests_step(uint32_t now_ms, DebugRunnerView view, const DebugAppHooks *app)
{
    DebugRequest request = debug_steps_step(now_ms, view);
    if (request.kind == DEBUG_REQUEST_START) {
        start(app, now_ms);
    } else if (request.kind != DEBUG_REQUEST_NONE) {
        finish(serve(app, &request, now_ms));
    }
}
