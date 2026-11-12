#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/frame_stats.h"
#include "gfx/rect.h"
#include "hal/power.h"

typedef enum AppUsbView {
    APP_USB_CONNECTING,
    APP_USB_STORAGE,
    APP_USB_DAC,
    APP_USB_POWER
} AppUsbView;

struct UsbScene;

struct HeadphoneProfiles;

typedef struct AppDacSettings {
    uint8_t volume;
    uint8_t gain;
    uint8_t output;
    uint8_t headphones;
} AppDacSettings;

typedef struct AppUsbStatus {
    AppUsbView view;
    const char *detail;
    const char *information;
    const struct UsbScene *scene;
} AppUsbStatus;

struct Canvas;
struct Theme;
typedef void (*AppUsbRenderer)(struct Canvas *canvas, const struct Theme *theme, HalBattery battery,
                               AppUsbStatus status);

void app_start(void);
int16_t app_clock_offset(void);
bool app_store_clock_offset(int16_t offset);
void app_step(void);
void app_stop(void);
const FrameStats *app_frame_stats(void);
const char *app_screen_title(void);
int app_screen_depth(void);
void app_show_notice(const char *title, const char *message);
void app_show_usb_status(AppUsbStatus status);
void app_show_usb_area(AppUsbStatus status, Rect area);
const struct Theme *app_theme(void);
void app_set_usb_renderer(AppUsbRenderer renderer);
bool app_usb_battery_changed(HalBattery before, HalBattery after);
void app_wake(void);
void app_open_usb_modes(void);
void app_ask_usb_mode(void);
void app_release_audio(void);
AppDacSettings app_dac_settings(void);
void app_remember_dac(AppDacSettings settings);
const struct HeadphoneProfiles *app_headphone_profiles(void);
void app_request_rescan(void);
void app_request_rescan_if_music_changed(void);
void app_show_splash(void);
void app_draw_start(void);
bool app_charging(void);
uint32_t app_pass_interval_ms(void);
bool app_preparing(void);

void app_storage_formatted(bool success);
