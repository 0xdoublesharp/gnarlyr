#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/msc.h"
#include "device/usb_audio.h"
#include "device/usb_audio_hid.h"
#include "device/usb_device.h"
#include "hal/system.h"

enum {
    USB_PORT_PACKET_BYTES = (int)USB_AUDIO_MAX_PACKET > (int)USB_HIGH_SPEED_PACKET_SIZE
        ? USB_AUDIO_MAX_PACKET
        : USB_HIGH_SPEED_PACKET_SIZE,
    USB_PORT_PACKET_WORDS = (USB_PORT_PACKET_BYTES + 3) / 4
};

typedef enum UsbConnectStep {
    USB_CONNECT_OK,
    USB_CONNECT_AHB_IDLE,
    USB_CONNECT_SOFT_RESET,
    USB_CONNECT_FLUSH
} UsbConnectStep;

typedef struct UsbProbe {
    uint32_t core_id;
    uint32_t otg_control;
    uint32_t hardware_3;
    uint32_t hardware_4;
    uint32_t interrupts;
    uint32_t rx_fifo_size;
    uint32_t tx_fifo_size;
} UsbProbe;

typedef struct UsbLink {
    UsbConnectStep step;
    bool high_speed_allowed;
    bool dedicated_fifos;
} UsbLink;

_Static_assert(USB_PORT_PACKET_BYTES >= 512, "a bulk IN chunk fits the packet buffer");

typedef struct UsbBurst {
    uint32_t start_ms;
    uint32_t idle_ms;
    bool served;
    bool listening;
} UsbBurst;

typedef struct UsbWriteTiming {
    uint64_t spent_us[MSC_WAIT_KINDS];
    uint32_t at_us;
    uint8_t wait;
} UsbWriteTiming;

typedef struct UsbPort {
    UsbLink link;
    bool configured;
    bool enumerated;
    bool in_busy;
    bool yield_when_idle;
    uint16_t in_left;
    bool setup_pending;
    bool setup_ready;
    HalUsbSetupTrace setups;
    bool defer_control;
    bool control_data_pending;
    uint16_t control_data_length;
    uint8_t control_data[4];
    uint16_t bulk_packet;
    uint8_t resets;
    uint32_t setup[2];
    bool control_out_pending;
    UsbSetup control_out;
    uint32_t out_packet[USB_PORT_PACKET_WORDS];
    UsbDevice device;
    Msc *msc;
    UsbAudio *audio;
    UsbAudioHid hid;
    uint32_t hid_sent_ms;
    uint32_t (*feedback)(uint16_t tick);
    void (*format_changed)(void);
    void (*receive_stream)(const uint8_t *bytes, uint32_t length);
} UsbPort;

bool usb_vbus_present(void);
void usb_power_up(void);
void usb_power_down(void);
void usb_silence_audio(void);
bool usb_host_reset_pending(void);
bool usb_connect(UsbLink *link);
bool usb_flush_fifos(void);
UsbProbe usb_probe(void);
bool usb_port_connect(UsbPort *port);
bool usb_wait_for_host(UsbPort *port, uint32_t timeout_ms);
void usb_poll(UsbPort *port);
void usb_poll_audio(UsbPort *port);
bool usb_reply_pending(const UsbPort *port);
UsbWriteTiming *usb_port_write_timing(void);
void usb_port_settle_timing(void);
