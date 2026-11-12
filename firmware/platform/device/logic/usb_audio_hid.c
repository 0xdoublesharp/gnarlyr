#include "device/usb_audio_hid.h"

enum {
    HOLD_MS = 400
};

static void enqueue(UsbAudioHid *hid, uint16_t usage)
{
    if (hid->count < USB_AUDIO_HID_QUEUE) {
        hid->queue[(hid->head + hid->count) % USB_AUDIO_HID_QUEUE] = usage;
        hid->count++;
    }
}

void usb_audio_hid_tick(UsbAudioHid *hid, uint32_t now_ms)
{
    for (uint8_t index = 0; index < 2; index++) {
        uint8_t mask = (uint8_t)(1u << index);
        if ((hid->held & mask) != 0 && (hid->skipped & mask) == 0 &&
            now_ms - hid->pressed_ms[index] >= HOLD_MS) {
            enqueue(hid, index == 0 ? USB_AUDIO_HID_NEXT : USB_AUDIO_HID_PREVIOUS);
            hid->skipped |= mask;
        }
    }
}

int usb_audio_hid_key(UsbAudioHid *hid, Key key, bool pressed, uint32_t now_ms)
{
    if (key == KEY_PLAY) {
        if (pressed) {
            enqueue(hid, USB_AUDIO_HID_PLAY_PAUSE);
        }
        return 0;
    }
    if (key != KEY_VOLUME_UP && key != KEY_VOLUME_DOWN) {
        return 0;
    }
    uint8_t index = key == KEY_VOLUME_UP ? 0 : 1;
    uint8_t mask = (uint8_t)(1u << index);
    if (pressed) {
        hid->pressed_ms[index] = now_ms;
        hid->held |= mask;
        hid->skipped &= (uint8_t)~mask;
        return 0;
    }
    usb_audio_hid_tick(hid, now_ms);
    bool tap = (hid->held & mask) != 0 && (hid->skipped & mask) == 0;
    hid->held &= (uint8_t)~mask;
    return tap ? (index == 0 ? 1 : -1) : 0;
}

bool usb_audio_hid_report(const UsbAudioHid *hid, uint16_t *usage)
{
    if (hid->count == 0) {
        return false;
    }
    *usage = hid->releasing ? 0 : hid->queue[hid->head];
    return true;
}

void usb_audio_hid_sent(UsbAudioHid *hid)
{
    if (hid->count == 0) {
        return;
    }
    if (hid->releasing) {
        hid->head = (uint8_t)((hid->head + 1) % USB_AUDIO_HID_QUEUE);
        hid->count--;
    }
    hid->releasing = !hid->releasing;
}
