#include "debug_screenshot.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"

enum {
    SLOT_DIGIT = sizeof "/" FIRMWARE_DATA_DIRECTORY "/debug/shot" - 1,
    NAME_OFFSET = 16
};

static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/debug/shot0.565";

typedef struct Capture {
    char path[sizeof PATH];
    bool written;
} Capture;

static void put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void fill_header(const DebugScreenshot *shot, uint8_t *header)
{
    uint32_t now_ms = hal_clock_milliseconds();
    header[0] = 'D';
    header[1] = 'S';
    header[2] = 'S';
    header[3] = 'H';
    header[4] = DEBUG_SCREENSHOT_VERSION;
    header[5] = DEBUG_SCREENSHOT_RGB565;
    put16(header + 6, (uint32_t)shot->renderer.screen_width);
    put16(header + 8, (uint32_t)shot->renderer.screen_height);
    put16(header + 10, DEBUG_SCREENSHOT_HEADER_BYTES);
    put16(header + 12, now_ms);
    put16(header + 14, now_ms >> 16);
    for (size_t index = 0; index < shot->name.length && index < DEBUG_SCRIPT_NAME_BYTES; index++) {
        header[NAME_OFFSET + index] = (uint8_t)shot->name.text[index];
    }
}

static void write_strip(void *context, Rect area, const Color *pixels)
{
    Capture *capture = context;
    uint32_t row_bytes = (uint32_t)area.width * sizeof(Color);
    uint32_t offset = DEBUG_SCREENSHOT_HEADER_BYTES + (uint32_t)area.y * row_bytes;
    hal_audio_service();
    capture->written =
        capture->written && hal_internal_file_write_at(capture->path, offset, pixels,
                                                       (uint32_t)area.height * row_bytes);
}

static bool reserved(const DebugScreenshot *shot, const char *path)
{
    uint32_t pixels =
        (uint32_t)shot->renderer.screen_width * (uint32_t)shot->renderer.screen_height;
    uint32_t size = DEBUG_SCREENSHOT_HEADER_BYTES + pixels * (uint32_t)sizeof(Color);
    HalReserveResult result = hal_internal_file_reserve(path, size);
    return result == HAL_RESERVE_EXISTS || result == HAL_RESERVE_CREATED;
}

bool debug_screenshot_save(const DebugScreenshot *shot)
{
    uint8_t header[DEBUG_SCREENSHOT_HEADER_BYTES] = {0};
    Capture capture = {.written = true};
    for (size_t index = 0; index < sizeof PATH; index++) {
        capture.path[index] = PATH[index];
    }
    capture.path[SLOT_DIGIT] = (char)('0' + shot->slot);
    if (shot->slot >= DEBUG_SCRIPT_SCREENSHOTS || !reserved(shot, capture.path) ||
        !hal_internal_file_write_at(capture.path, 0, header, sizeof header)) {
        return false;
    }
    FrameSink sink = {.write = write_strip, .context = &capture};
    strip_renderer_render(&shot->renderer, shot->drawer, sink);
    fill_header(shot, header);
    return capture.written && hal_internal_file_write_at(capture.path, 0, header, sizeof header);
}
