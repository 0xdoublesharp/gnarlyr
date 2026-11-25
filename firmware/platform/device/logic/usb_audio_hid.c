#include "device/usb_audio_hid.h"

#include "core/deadline.h"

enum {
    HOLD_MS = 350,
    REPEAT_MS = 75
};

int usb_audio_hid_tick(UsbAudioHid *hid, uint32_t now_ms)
{
    if (hid->volume_step == 0 || !deadline_reached(now_ms, hid->repeat_ms)) {
        return 0;
    }
    hid->repeat_ms = now_ms + REPEAT_MS;
    return hid->volume_step;
}

int usb_audio_hid_key(UsbAudioHid *hid, Key key, bool pressed, uint32_t now_ms)
{
    if (key == KEY_PLAY) {
        if (pressed) {
            usb_audio_hid_enqueue(hid, USB_AUDIO_HID_PLAY_PAUSE);
        }
        return 0;
    }
    if (key != KEY_VOLUME_UP && key != KEY_VOLUME_DOWN) {
        return 0;
    }
    int8_t step = key == KEY_VOLUME_UP ? 1 : -1;
    if (step != hid->volume_step && pressed) {
        hid->volume_step = step;
        hid->repeat_ms = now_ms + HOLD_MS;
        return step;
    }
    if (step == hid->volume_step && !pressed) {
        hid->volume_step = 0;
    }
    return 0;
}

bool usb_audio_hid_enqueue(UsbAudioHid *hid, uint8_t usage)
{
    if (usage == 0 || hid->count == USB_AUDIO_HID_QUEUE) {
        return false;
    }
    hid->queue[(hid->head + hid->count) % USB_AUDIO_HID_QUEUE] = usage;
    hid->count++;
    return true;
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
        hid->count--;
        hid->head = (uint8_t)((hid->head + 1) % USB_AUDIO_HID_QUEUE);
    }
    hid->releasing = !hid->releasing;
}
