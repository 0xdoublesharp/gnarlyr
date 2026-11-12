#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/app.h"
#include "gfx/canvas.h"
#include "ui/list_view.h"
#include "ui/theme.h"
#include "usb_meter.h"

enum {
    USB_SCENE_DRIVES = 2,
    USB_SCENE_CHANNELS = 2,
    USB_SCENE_LEVEL_FULL = 255,
    USB_SCREEN_BAND_HEIGHT = 20,
    USB_SCREEN_BAND_WIDTH = 160,
    USB_SCENE_OVERVIEW = 0
};

#define USB_SCENE_FREE_UNKNOWN 0xFFFFFFFFu

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

typedef struct UsbSceneUsage {
    uint32_t free_sectors;
    uint32_t read_sectors;
    uint32_t written_sectors;
    uint32_t reads;
    uint32_t writes;
    bool active;
    uint8_t read_glow;
    uint8_t write_glow;
} UsbSceneUsage;

typedef struct UsbSceneLink {
    uint32_t seconds;
    uint32_t errors;
    uint16_t packet;
    uint8_t busy_percent;
    uint8_t sense_key;
    uint8_t sense_code;
} UsbSceneLink;

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
    uint8_t channels;
    uint8_t alt;
    uint8_t gain_register;
    bool gain_high;
    bool asynchronous;
    uint16_t buffer_low_ms;
    uint16_t buffer_high_ms;
    uint16_t buffer_average_ms;
    uint32_t overruns;
    uint32_t feedback;
    uint32_t data_rate;
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
    void (*service)(void);
    uint8_t page;
    UsbMeter meter;
    UsbSceneLink link;
    UsbSceneUsage usage[USB_SCENE_DRIVES];
    bool busy;
    bool lit[USB_METER_DIRECTIONS];
    uint32_t rate;
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
int usb_screen_pages(const Theme *theme, AppUsbStatus status);
Rect usb_screen_graph(const Theme *theme, const UsbScreenLayout *layout);
Rect usb_screen_content(const Theme *theme, const UsbScreenLayout *layout);
Rect usb_screen_drive_lights(const Theme *theme, AppUsbStatus status, int lun);
Rect usb_screen_activity(const Theme *theme, AppUsbStatus status);
