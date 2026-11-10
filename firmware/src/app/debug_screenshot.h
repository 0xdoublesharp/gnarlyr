#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/debug_script.h"
#include "gfx/strip_renderer.h"

enum {
    DEBUG_SCREENSHOT_HEADER_BYTES = 32,
    DEBUG_SCREENSHOT_VERSION = 1,
    DEBUG_SCREENSHOT_RGB565 = 1
};

typedef struct DebugScreenshot {
    StripRenderer renderer;
    FrameDrawer drawer;
    DebugSpan name;
    uint32_t slot;
} DebugScreenshot;

#ifdef ECHO_DEBUG_LOOP

bool debug_screenshot_save(const DebugScreenshot *shot);

#else

static inline bool debug_screenshot_save(const DebugScreenshot *shot)
{
    (void)shot;
    return false;
}

#endif
