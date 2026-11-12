#include <stddef.h>
#include <stdint.h>

#include "app/app.h"
#include "debug_probe.h"
#include "gfx/strip_renderer.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "screens/charge_screen.h"
#include "screens/usb_scene.h"
#include "usb_session.h"

enum {
    HEADER_BYTES = 32,
    NAME_AT = 16,
    VERSION = 1,
    RGB565 = 1,
    SLOT_DIGIT = sizeof "/" FIRMWARE_DATA_DIRECTORY "/debug/usbshot" - 1,
    PAGE_LIMIT = 3
};

static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/debug/usbshot0.565";

typedef struct Capture {
    char path[sizeof PATH];
    AppUsbStatus status;
    HalBattery battery;
    bool written;
} Capture;

static void put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void fill_header(uint8_t *header, Rect area, const char *name, uint8_t page)
{
    uint32_t now_ms = hal_clock_milliseconds();
    header[0] = 'D';
    header[1] = 'S';
    header[2] = 'S';
    header[3] = 'H';
    header[4] = VERSION;
    header[5] = RGB565;
    put16(header + 6, (uint32_t)area.width);
    put16(header + 8, (uint32_t)area.height);
    put16(header + 10, HEADER_BYTES);
    put16(header + 12, now_ms);
    put16(header + 14, now_ms >> 16);
    size_t index = 0;
    for (; name[index] != '\0'; index++) {
        header[NAME_AT + index] = (uint8_t)name[index];
    }
    header[NAME_AT + index] = (uint8_t)('0' + page);
}

static void draw(void *context, Canvas *canvas)
{
    const Capture *capture = context;
    usb_screen_draw(canvas, app_theme(), capture->battery, capture->status);
}

static void write_strip(void *context, Rect area, const Color *pixels)
{
    Capture *capture = context;
    uint32_t row_bytes = (uint32_t)area.width * sizeof(Color);
    uint32_t offset = HEADER_BYTES + (uint32_t)area.y * row_bytes;
    capture->written =
        capture->written && hal_internal_file_write_at(capture->path, offset, pixels,
                                                       (uint32_t)area.height * row_bytes);
}

static void capture_page(Capture *capture, const char *name, uint8_t page, uint8_t slot)
{
    const Theme *theme = app_theme();
    Rect area = {0, 0, theme->metrics.screen_width, theme->metrics.screen_height};
    uint8_t header[HEADER_BYTES] = {0};
    uint32_t size = HEADER_BYTES + (uint32_t)(area.width * area.height) * sizeof(Color);
    capture->path[SLOT_DIGIT] = (char)('0' + slot);
    HalReserveResult reserved = hal_internal_file_reserve(capture->path, size);
    if (reserved != HAL_RESERVE_EXISTS && reserved != HAL_RESERVE_CREATED) {
        return;
    }
    StripRenderer renderer = {.strip_pixels = (Color *)(void *)usb_session_buffer(),
                              .area = area,
                              .strip_height =
                                  USB_SESSION_BUFFER_BYTES / (area.width * (int)sizeof(Color)),
                              .background = theme->colors.background,
                              .drawer_paints_everything = true};
    capture->written = true;
    strip_renderer_render(&renderer, (FrameDrawer){draw, capture},
                          (FrameSink){write_strip, capture});
    fill_header(header, area, name, page);
    if (capture->written) {
        hal_internal_file_write_at(capture->path, 0, header, sizeof header);
    }
}

void usb_screen_capture(AppUsbStatus status, UsbScene *scene, const char *name, uint8_t slot)
{
    Capture capture = {.status = status, .battery = hal_power_battery()};
    for (size_t index = 0; index < sizeof PATH; index++) {
        capture.path[index] = PATH[index];
    }
    uint8_t shown = scene->page;
    scene->service = NULL;
    int pages = usb_screen_pages(app_theme(), status);
    for (uint8_t page = 0; page < pages && page < PAGE_LIMIT; page++) {
        scene->page = page;
        capture_page(&capture, name, page, (uint8_t)(slot + page));
    }
    scene->page = shown;
}
