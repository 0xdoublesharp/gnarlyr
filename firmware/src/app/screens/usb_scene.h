#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/app.h"
#include "gfx/canvas.h"
#include "ui/list_view.h"
#include "ui/theme.h"

enum {
    USB_SCENE_DRIVES = 2,
    USB_SCENE_CHANNELS = 2,
    USB_SCENE_LEVEL_FULL = 255,
    USB_SCREEN_BAND_HEIGHT = 20
};

typedef enum UsbSceneState {
    USB_SCENE_STARTING,
    USB_SCENE_CONNECTED,
    USB_SCENE_READING,
    USB_SCENE_WRITING,
    USB_SCENE_WAITING,
    USB_SCENE_LEAVING,
    USB_SCENE_UNPLUGGED,
    USB_SCENE_STOPPED,
    USB_SCENE_DAC_CHOOSE,
    USB_SCENE_DAC_READY,
    USB_SCENE_DAC_PLAYING,
    USB_SCENE_DAC_FAILED
} UsbSceneState;

typedef enum UsbSceneDriveState {
    USB_SCENE_DRIVE_ABSENT,
    USB_SCENE_DRIVE_MOUNTED,
    USB_SCENE_DRIVE_EJECTED
} UsbSceneDriveState;

typedef struct UsbSceneDrive {
    uint32_t sectors;
    uint8_t state;
} UsbSceneDrive;

typedef struct UsbSceneDac {
    const char *headphones;
    uint32_t rate;
    uint32_t seconds;
    uint32_t underruns;
    int32_t drift_ppm;
    int16_t host_volume;
    uint16_t buffer_ms;
    uint8_t bits;
    uint8_t volume;
    uint8_t jack;
    bool host_volume_known;
    bool host_muted;
    uint8_t level[USB_SCENE_CHANNELS];
} UsbSceneDac;

typedef struct UsbScene {
    uint8_t state;
    uint8_t turn;
    bool holding;
    bool high_speed;
    uint32_t written_kib;
    UsbSceneDrive drives[USB_SCENE_DRIVES];
    UsbSceneDac dac;
    const ListView *menu;
} UsbScene;

typedef struct UsbScreenLayout {
    Rect header;
    Rect art;
    Rect figure;
    Rect state;
    Rect lines;
    Rect meter;
    Rect detail;
} UsbScreenLayout;

UsbScreenLayout usb_screen_layout(const Theme *theme, AppUsbStatus status);
Rect usb_screen_column(const UsbScreenLayout *layout);
void usb_screen_show_in_bands(AppUsbStatus status, Rect area);
Rect usb_screen_meter_bar(const Theme *theme, const UsbScreenLayout *layout, int side);
