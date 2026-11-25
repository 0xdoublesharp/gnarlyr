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

static void volume_steps_on_press_and_repeats_locally_until_release(void)
{
    UsbAudioHid hid = {0};
    CHECK_EQUAL(1, usb_audio_hid_key(&hid, KEY_VOLUME_UP, true, 0));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 349));
    CHECK_EQUAL(1, usb_audio_hid_tick(&hid, 350));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 350));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 424));
    CHECK_EQUAL(1, usb_audio_hid_tick(&hid, 425));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_UP, false, 500));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 600));
    CHECK_EQUAL(-1, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, true, 650));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, false, 700));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 1500));
    CHECK_EQUAL(0, hid.count);
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

static void the_latest_volume_press_repeats_across_clock_wrap(void)
{
    UsbAudioHid hid = {0};
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_UP, false, 10));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_MENU, true, 20));
    usb_audio_hid_sent(&hid);
    uint16_t usage = 0xA5A5;
    CHECK(!usb_audio_hid_report(&hid, &usage));
    CHECK_EQUAL(0xA5A5, usage);
    CHECK_EQUAL(1, usb_audio_hid_key(&hid, KEY_VOLUME_UP, true, UINT32_MAX - 200));
    CHECK_EQUAL(-1, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, true, UINT32_MAX - 100));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_UP, false, 10));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, true, 100));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 248));
    CHECK_EQUAL(-1, usb_audio_hid_tick(&hid, 249));
    CHECK_EQUAL(-1, usb_audio_hid_tick(&hid, 400));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 401));
    CHECK_EQUAL(0, usb_audio_hid_key(&hid, KEY_VOLUME_DOWN, false, 450));
    CHECK_EQUAL(0, usb_audio_hid_tick(&hid, 500));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(play_sends_one_press_then_one_release),
        TEST_CASE(volume_steps_on_press_and_repeats_locally_until_release),
        TEST_CASE(a_full_queue_keeps_every_release_paired_with_its_press),
        TEST_CASE(the_latest_volume_press_repeats_across_clock_wrap),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
