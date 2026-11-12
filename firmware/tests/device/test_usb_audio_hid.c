#include "check.h"
#include "device/usb_audio_hid.h"

static void take(UsbAudioHid *hid, uint16_t expected)
{
    uint16_t usage = 0xFFFF;
    CHECK(usb_audio_hid_report(hid, &usage));
    CHECK_EQUAL(expected, usage);
    usb_audio_hid_sent(hid);
}

static void play_sends_one_press_then_one_release(void)
{
    UsbAudioHid hid = {0};
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_PLAY, true, 0));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_PLAY, false, 100));
    take(&hid, USB_AUDIO_HID_PLAY_PAUSE);
    take(&hid, 0);
    uint16_t usage;
    CHECK(!usb_audio_hid_report(&hid, &usage));
}

static void volume_taps_stay_local_and_holds_skip_once(void)
{
    UsbAudioHid hid = {0};
    usb_audio_hid_key(&hid, KEY_VOLUME_UP, true, 0);
    CHECK_EQUAL(1, usb_audio_hid_key(&hid, KEY_VOLUME_UP, false, 399));
    usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, true, 500);
    CHECK_EQUAL(-1, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, false, 700));
    CHECK_EQUAL(0, hid.count);
    usb_audio_hid_key(&hid, KEY_VOLUME_UP, true, 1000);
    usb_audio_hid_tick(&hid, 1400);
    usb_audio_hid_tick(&hid, 3000);
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_UP, false, 3001));
    usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, true, UINT32_MAX - 100);
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, false, 300));
    take(&hid, USB_AUDIO_HID_NEXT);
    take(&hid, 0);
    take(&hid, USB_AUDIO_HID_PREVIOUS);
    take(&hid, 0);
}

static void a_full_queue_keeps_every_release_paired_with_its_press(void)
{
    UsbAudioHid hid = {0};
    for (int index = 0; index < 20; index++) {
        usb_audio_hid_key(&hid, KEY_PLAY, true, 0);
    }
    CHECK_EQUAL(USB_AUDIO_HID_QUEUE, hid.count);
    for (int index = 0; index < USB_AUDIO_HID_QUEUE; index++) {
        take(&hid, USB_AUDIO_HID_PLAY_PAUSE);
        take(&hid, 0);
    }
    CHECK_EQUAL(0, hid.count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(play_sends_one_press_then_one_release),
        TEST_CASE(volume_taps_stay_local_and_holds_skip_once),
        TEST_CASE(a_full_queue_keeps_every_release_paired_with_its_press),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
