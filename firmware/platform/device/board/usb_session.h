#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/app.h"
#include "device/usb_forced.h"
#include "emmc.h"
#include "hal/system.h"
#include "usb.h"

typedef enum UsbSessionEnd {
    USB_SESSION_NO_HOST,
    USB_SESSION_EJECTED,
    USB_SESSION_UNPLUGGED,
    USB_SESSION_STOPPED,
    USB_SESSION_FAILED
} UsbSessionEnd;

typedef struct UsbReport {
    bool internal_ready;
    UsbConnectStep connect_step;
    UsbProbe after_power_up;
    UsbProbe after_wait;
} UsbReport;

typedef struct UsbReconnect {
    bool requested;
    bool forced;
    bool manual;
    bool in_session;
    uint32_t ended_ms;
    uint32_t forced_ms;
} UsbReconnect;

typedef struct UsbShared {
    UsbLink link;
    UsbReport report;
    EmmcInfo internal;
    HalUsbLast last_session;
    UsbReconnect reconnect;
    bool show_modes;
    bool high_speed_failed;
    bool flash_rewritten;
    bool dac_volume_known;
    uint8_t dac_volume;
    uint8_t usb_page;
} UsbShared;

typedef struct UsbOverlayEntry {
    uint32_t magic;
    bool (*storage)(UsbShared *shared);
    void (*dac)(UsbShared *shared);
    AppUsbRenderer draw;
#ifdef ECHO_DEBUG_LOOP
    void (*report)(const UsbShared *shared, uint32_t reason);
#endif
} UsbOverlayEntry;

void usb_session_request_reconnect(bool on);
bool usb_session_take_reconnect(void);
bool usb_session_forced(void);
bool usb_session_connect_requested(void);
bool usb_session_reconnect_stopped(void);
void usb_session_reconnect_started(void);
void usb_session_reconnect_ended(void);
bool usb_session_present(UsbShared *state, UsbForced *forced, uint32_t commands);
bool usb_session_listen(void);
bool usb_dac_session_listen(void);
bool usb_session_host_seen(void);
void usb_session_stop_listening(void);
const UsbReport *usb_session_report(void);
UsbShared *usb_session_shared(void);

bool usb_storage_session(UsbShared *shared);
bool usb_session_storage_changed(void);
enum {
    USB_SESSION_BUFFER_BYTES = 64 * 512
};

uint32_t *usb_session_buffer(void);
void usb_dac_session(UsbShared *shared);
void update_after_usb(UsbShared *shared);
void time_file_take(bool apply);
