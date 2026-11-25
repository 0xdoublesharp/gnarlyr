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
        if (pressed && hid->count < USB_AUDIO_HID_QUEUE) {
            hid->count++;
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

bool usb_audio_hid_report(const UsbAudioHid *hid, uint16_t *usage)
{
    if (hid->count == 0) {
        return false;
    }
    *usage = hid->releasing ? 0 : USB_AUDIO_HID_PLAY_PAUSE;
    return true;
}

void usb_audio_hid_sent(UsbAudioHid *hid)
{
    if (hid->count == 0) {
        return;
    }
    if (hid->releasing) {
        hid->count--;
    }
    hid->releasing = !hid->releasing;
}
