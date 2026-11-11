#include "usb_session.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "device/usb_audio.h"
#include "device/usb_audio_sink.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "settings/settings.h"

enum {
    HOST_TIMEOUT_MS = 3000,
    BATTERY_REFRESH_MS = 1000,
    STREAM_IDLE_MS = 100,
    BACK_HOLD_MS = 1000,
    POLLS_PER_SERVICE = 4,
    NOTICE_CAPACITY = 96,
    NUMBER_CAPACITY = 12,
    HZ_PER_KHZ = 1000,
    BITS_PER_BYTE = 8,
    VOLUME_REPEAT_DELAY_MS = 400,
    VOLUME_REPEAT_MS = 80
};

static UsbShared *shared;
static UsbPort port;
static UsbAudio audio;
static UsbAudioSink sink;
static uint32_t open_rate;
static uint32_t last_packet_ms;
static uint32_t received_packets;
static UsbForced forced;
static uint32_t back_pressed_ms;
static bool receiving;
static bool playing;
static bool holding_volume;
static int held_step;
static uint32_t next_repeat_ms;

static void receive_stream(const uint8_t *bytes, uint32_t length)
{
    received_packets++;
    if (receiving) {
        last_packet_ms = hal_clock_milliseconds();
        usb_audio_sink_write(&sink, bytes, length, board_audio_stream_consumed());
    }
}

static void serve_usb(void)
{
    for (int poll = 0; poll < POLLS_PER_SERVICE; poll++) {
        usb_poll(&port);
    }
}

static void write_number(char *text, uint32_t value)
{
    StringBuilder builder = string_builder_make(text, NUMBER_CAPACITY);
    string_builder_append_unsigned(&builder, value);
}

static void write_rate(char *text)
{
    StringBuilder builder = string_builder_make(text, NUMBER_CAPACITY);
    string_builder_append_unsigned(&builder, audio.rate / HZ_PER_KHZ);
    if (audio.rate % HZ_PER_KHZ != 0) {
        string_builder_append_char(&builder, '.');
        string_builder_append_unsigned(&builder, audio.rate % HZ_PER_KHZ / 100);
    }
}

static void append_format(StringBuilder *line, const char *volume)
{
    char rate[NUMBER_CAPACITY];
    char bits[NUMBER_CAPACITY];
    write_rate(rate);
    write_number(bits, BITS_PER_BYTE * usb_audio_sample_bytes(&audio));
    const char *arguments[] = {rate, bits, volume};
    i18n_format(line, STR_USB_DAC_FORMAT, arguments, 3);
}

static void show_state(void)
{
    char text[NOTICE_CAPACITY];
    char volume[NUMBER_CAPACITY];
    StringBuilder line = string_builder_make(text, sizeof text);
    write_number(volume, shared->dac_volume);
    if (usb_audio_sample_bytes(&audio) == 0) {
        const char *arguments[] = {FIRMWARE_SHORT_NAME, volume};
        i18n_format(&line, STR_USB_DAC_CHOOSE, arguments, 2);
    } else if (open_rate == 0) {
        string_builder_append(&line, i18n_text(STR_USB_DAC_FAILED));
    } else {
        append_format(&line, volume);
    }
    app_show_usb_status(
        (AppUsbStatus){.view = APP_USB_DAC, .detail = text, .volume = shared->dac_volume});
}

static void stop_output(void)
{
    if (playing) {
        board_audio_stream_pause();
        playing = false;
    }
    usb_audio_sink_start(&sink, audio.rate, usb_audio_sample_bytes(&audio));
}

static void follow_format(void)
{
    receiving = false;
    stop_output();
    if (usb_audio_sample_bytes(&audio) != 0 && audio.rate != open_rate) {
        open_rate = board_audio_stream_open(audio.rate, serve_usb) ? audio.rate : 0;
    }
    receiving = open_rate != 0 && usb_audio_sample_bytes(&audio) != 0;
    show_state();
}

static void follow_host(void)
{
    uint8_t changes = usb_audio_take_changes(&audio);
    if ((changes & USB_AUDIO_CHANGED_VOLUME) != 0) {
        board_audio_set_attenuation(usb_audio_attenuation(&audio));
    }
    if ((changes & USB_AUDIO_CHANGED_FORMAT) != 0) {
        follow_format();
    }
}

static void keep_output(void)
{
    if (playing && hal_clock_milliseconds() - last_packet_ms > STREAM_IDLE_MS) {
        stop_output();
    } else if (!playing && receiving && usb_audio_sink_primed(&sink)) {
        playing = board_audio_stream_play();
        usb_audio_sink_set_playing(&sink, playing);
    }
}

static void step_volume(int step)
{
    int level = shared->dac_volume + step;
    if (level < 0 || level > HAL_AUDIO_VOLUME_MAX) {
        return;
    }
    shared->dac_volume = headphone_audio_limit(hal_audio_headphones(), (uint8_t)level);
    hal_audio_set_volume(shared->dac_volume);
    show_state();
}

static void start_holding(int step, uint32_t now_ms)
{
    step_volume(step);
    holding_volume = true;
    held_step = step;
    next_repeat_ms = now_ms + VOLUME_REPEAT_DELAY_MS;
}

static void repeat_volume(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    if (holding_volume && (int32_t)(now_ms - next_repeat_ms) >= 0) {
        step_volume(held_step);
        next_repeat_ms = now_ms + VOLUME_REPEAT_MS;
    }
}

static void follow_volume_key(HalKeyTransition key)
{
    int step = key.key == KEY_VOLUME_UP ? 1 : -1;
    if (key.pressed) {
        start_holding(step, key.time_ms);
    } else if (holding_volume && held_step == step) {
        holding_volume = false;
    }
}

static bool back_released_after_hold(HalKeyTransition key)
{
    if (key.pressed) {
        back_pressed_ms = key.time_ms;
        return false;
    }
    return key.time_ms - back_pressed_ms >= BACK_HOLD_MS;
}

static bool user_left(void)
{
    HalKeyTransition key;
    while (hal_input_next_transition(&key)) {
        if (key.key == KEY_BACK && back_released_after_hold(key)) {
            return true;
        }
        if (key.key == KEY_VOLUME_UP || key.key == KEY_VOLUME_DOWN) {
            follow_volume_key(key);
        }
    }
    return false;
}

static bool serve(void)
{
    back_pressed_ms = hal_clock_milliseconds();
    uint32_t refreshed = back_pressed_ms;
    HalBattery shown_battery = hal_power_battery();
    while (usb_session_present(shared, &forced, received_packets)) {
        if (user_left()) {
            return true;
        }
        serve_usb();
        hal_audio_service();
        board_display_service();
        follow_host();
        keep_output();
        repeat_volume();
        uint32_t now = hal_clock_milliseconds();
        if (now - refreshed >= BATTERY_REFRESH_MS) {
            HalBattery battery = hal_power_battery();
            if (app_usb_battery_changed(shown_battery, battery)) {
                show_state();
            }
            shown_battery = battery;
            refreshed = now;
        }
    }
    return false;
}

static void start_session(void)
{
    uint32_t frames = 0;
    uint32_t *ring = board_audio_stream_ring(&frames);
    usb_audio_sink_init(&sink, ring, frames);
    usb_audio_sink_set_headphones(&sink, hal_audio_headphones());
    open_rate = 0;
    received_packets = 0;
    forced = (UsbForced){0};
    receiving = false;
    playing = false;
    holding_volume = false;
    port = (UsbPort){.link = shared->link,
                     .bulk_packet = USB_PACKET_SIZE,
                     .audio = &audio,
                     .receive_stream = receive_stream};
    usb_audio_init(&audio, &port.device);
    if (!shared->dac_volume_known) {
        shared->dac_volume = (uint8_t)SETTINGS_DEFAULT_DAC_VOLUME;
    }
    shared->dac_volume = headphone_audio_limit(hal_audio_headphones(), shared->dac_volume);
    hal_audio_set_volume(shared->dac_volume);
}

void usb_dac_session(UsbShared *state)
{
    shared = state;
    app_show_usb_status(
        (AppUsbStatus){.view = APP_USB_CONNECTING, .detail = i18n_text(STR_USB_STARTING)});
    uint8_t level = board_audio_volume_level();
    start_session();
    uint8_t starting_volume = shared->dac_volume;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        show_state();
        shared->show_modes = serve();
    }
    receiving = false;
    stop_output();
    board_audio_stream_close();
    board_audio_set_attenuation(0);
    hal_audio_set_volume(level);
    usb_power_down();
    if (shared->dac_volume != starting_volume) {
        app_remember_dac_volume(shared->dac_volume);
    }
}
