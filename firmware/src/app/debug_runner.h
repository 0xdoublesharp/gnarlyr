#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "core/debug_script.h"
#include "core/frame_stats.h"
#include "debug_screenshot.h"
#include "hal/input.h"
#include "ui/screen.h"

typedef enum DebugRequestKind {
    DEBUG_REQUEST_NONE,
    DEBUG_REQUEST_START,
    DEBUG_REQUEST_SET,
    DEBUG_REQUEST_SCREENSHOT,
    DEBUG_REQUEST_SCREEN,
    DEBUG_REQUEST_STANDBY,
    DEBUG_REQUEST_RESCAN,
    DEBUG_REQUEST_OPEN
} DebugRequestKind;

typedef struct DebugRequest {
    DebugRequestKind kind;
    DebugSpan argument;
    DebugSpan value;
    uint32_t slot;
} DebugRequest;

typedef struct DebugRunnerView {
    bool held;
    bool screen_on;
    bool power_down;
} DebugRunnerView;

typedef struct DebugFrameSources {
    const FrameStats *total;
    const FrameStats *update;
    const FrameStats *draw;
    const FrameStats *send;
} DebugFrameSources;

DebugFrameSources app_debug_frame_sources(void);

struct AppScreens;
struct Player;
struct PowerPolicy;
struct SettingsStore;

typedef struct DebugAppHooks {
    struct PowerPolicy *power;
    struct Player *player;
    struct AppScreens *screens;
    struct SettingsStore *settings;
    bool *charging;
    bool *rescan_manual;
    bool *rescan_requested;
    bool *render_urgent;
    void (*stop_charging)(uint32_t now_ms);
    void (*settle)(uint32_t now_ms);
    bool (*apply)(ScreenAction action, uint32_t now_ms);
    void (*open_start)(uint32_t start, uint32_t now_ms);
    void (*changed)(void);
    void (*frame)(DebugScreenshot *shot);
} DebugAppHooks;

typedef struct DebugCodeEntry {
    void (*step)(uint32_t now_ms, DebugRunnerView view, const DebugAppHooks *app);
    void (*stop)(uint32_t now_ms, DebugScriptState state);
    void (*frame)(DebugFrameSources sources);
    bool (*screenshot)(const DebugScreenshot *shot);
} DebugCodeEntry;

#ifdef ECHO_DEBUG_LOOP

extern const DebugCodeEntry DEBUG_CODE_ENTRY;

#define DEBUG_CODE (&DEBUG_CODE_ENTRY)

bool debug_runner_active(void);
void debug_runner_step(uint32_t now_ms, DebugRunnerView view, const DebugAppHooks *app);
void debug_runner_key_pressed(void);
bool debug_runner_next_key(HalKeyTransition *transition);
void debug_runner_frame(DebugFrameSources sources);
bool debug_runner_screenshot(const DebugScreenshot *shot);

#else

#define DEBUG_CODE NULL

static inline bool debug_runner_active(void)
{
    return false;
}

static inline void debug_runner_step(uint32_t now_ms, DebugRunnerView view,
                                     const DebugAppHooks *app)
{
    (void)now_ms;
    (void)view;
    (void)app;
}

static inline void debug_runner_key_pressed(void)
{
}

static inline bool debug_runner_next_key(HalKeyTransition *transition)
{
    (void)transition;
    return false;
}

static inline void debug_runner_frame(DebugFrameSources sources)
{
    (void)sources;
}

static inline bool debug_runner_screenshot(const DebugScreenshot *shot)
{
    (void)shot;
    return false;
}

#endif
