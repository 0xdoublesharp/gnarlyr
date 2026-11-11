#include "usb_session.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/headphone_audio.h"
#include "core/i18n.h"
#include "device/usb_audio.h"
#include "device/usb_audio_sink.h"
#include "device/usb_level.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "headphone_store.h"
#include "screens/usb_dac_menu.h"
#include "screens/usb_scene.h"
#include "settings/settings.h"

enum {
    HOST_TIMEOUT_MS = 3000,
    INFO_REFRESH_MS = 1000,
    METER_FRAME_MS = 80,
    STREAM_IDLE_MS = 100,
    BACK_HOLD_MS = 1000,
    POLLS_PER_SERVICE = 4,
    MS_PER_SECOND = 1000,
    PPM = 1000000,
    VOLUME_REPEAT_DELAY_MS = 400,
    VOLUME_REPEAT_MS = 80
};

typedef enum DacNext {
    DAC_NEXT_STAY,
    DAC_NEXT_MODES,
    DAC_NEXT_STORAGE,
    DAC_NEXT_CHARGE
} DacNext;

static UsbShared *shared;
static UsbPort port;
static UsbAudio audio;
static UsbAudioSink sink;
static UsbScene scene;
static UsbDacMenu menu;
static AppDacSettings settings;
static uint32_t open_rate;
static uint32_t last_packet_ms;
static uint32_t received_packets;
static uint32_t played_packets;
static uint32_t connected_ms;
static uint32_t meter_ms;
static uint32_t underruns_before;
static UsbForced forced;
static uint32_t back_pressed_ms;
static bool back_armed;
static bool menu_open;
static bool receiving;
static bool playing;
static bool holding_volume;
static int held_step;
static uint32_t next_repeat_ms;
static DacNext next;

static void receive_stream(const uint8_t *bytes, uint32_t length)
{
    received_packets++;
    if (receiving) {
        last_packet_ms = hal_clock_milliseconds();
        usb_audio_sink_write(&sink, bytes, length, board_audio_stream_consumed());
        played_packets += playing ? 1u : 0u;
    }
}

static void serve_usb(void)
{
    for (int poll = 0; poll < POLLS_PER_SERVICE; poll++) {
        usb_poll(&port);
    }
}

static uint8_t stream_state(void)
{
    if (usb_audio_sample_bytes(&audio) == 0) {
        return USB_SCENE_DAC_CHOOSE;
    }
    if (open_rate == 0) {
        return USB_SCENE_DAC_FAILED;
    }
    return playing ? USB_SCENE_DAC_PLAYING : USB_SCENE_DAC_READY;
}

static int32_t drift_ppm(void)
{
    UsbAudioSinkStats stats = sink.stats;
    uint64_t frames = (uint64_t)played_packets * audio.rate / MS_PER_SECOND;
    if (frames == 0) {
        return 0;
    }
    int64_t moved = (int64_t)stats.repeated - (int64_t)stats.dropped;
    return (int32_t)(moved * PPM / (int64_t)frames);
}

static uint16_t buffer_ms(void)
{
    if (!playing || audio.rate == 0) {
        return 0;
    }
    uint32_t fill = sink.written - board_audio_stream_consumed();
    return (uint16_t)(fill < sink.ring_frames ? fill * MS_PER_SECOND / audio.rate : 0);
}

static void follow_info(uint32_t now_ms)
{
    UsbSceneDac *dac = &scene.dac;
    scene.state = stream_state();
    scene.high_speed = port.device.speed == USB_SPEED_HIGH;
    dac->rate = audio.rate;
    dac->bits = (uint8_t)(8 * usb_audio_sample_bytes(&audio));
    dac->volume = shared->dac_volume;
    dac->host_volume = audio.volume;
    dac->host_muted = audio.muted;
    dac->jack = (uint8_t)hal_audio_jack();
    dac->drift_ppm = drift_ppm();
    dac->buffer_ms = buffer_ms();
    dac->underruns = hal_audio_stats().output_underruns - underruns_before;
    dac->seconds = (now_ms - connected_ms) / MS_PER_SECOND;
    const HeadphoneProfiles *profiles = app_headphone_profiles();
    dac->headphones =
        settings.headphones < profiles->count ? profiles->slots[settings.headphones].name : NULL;
}

static AppUsbStatus dac_status(void)
{
    StringId hint = STR_USB_DAC_HINT;
    if (menu_open) {
        hint = menu.adjusting ? STR_USB_DAC_ADJUST_HINT : STR_USB_DAC_MENU_HINT;
    }
    scene.menu = menu_open ? &menu.view : NULL;
    return (AppUsbStatus){.view = APP_USB_DAC, .detail = i18n_text(hint), .scene = &scene};
}

static void show_state(void)
{
    follow_info(hal_clock_milliseconds());
    app_show_usb_status(dac_status());
}

static uint8_t follow_levels(uint32_t now_ms)
{
    uint16_t peaks[USB_SCENE_CHANNELS];
    usb_audio_sink_take_peaks(&sink, peaks);
    uint8_t changed = 0;
    for (int side = 0; side < USB_SCENE_CHANNELS; side++) {
        uint8_t now = playing ? usb_level_of(peaks[side]) : 0;
        uint8_t level = usb_level_follow(scene.dac.level[side], now, now_ms - meter_ms);
        changed |= (uint8_t)(level != scene.dac.level[side] ? 1u << side : 0u);
        scene.dac.level[side] = level;
    }
    return changed;
}

static void step_meter(uint32_t now_ms)
{
    if (menu_open || now_ms - meter_ms < METER_FRAME_MS) {
        return;
    }
    uint8_t changed = follow_levels(now_ms);
    meter_ms = now_ms;
    AppUsbStatus status = dac_status();
    const Theme *theme = app_theme();
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    for (int side = 0; side < USB_SCENE_CHANNELS; side++) {
        if ((changed & (1u << side)) != 0) {
            app_show_usb_area(status, usb_screen_meter_bar(theme, &layout, side));
        }
    }
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
        scene.dac.host_volume_known = true;
    }
    if ((changes & USB_AUDIO_CHANGED_FORMAT) != 0) {
        follow_format();
    } else if (changes != 0) {
        show_state();
    }
}

static void keep_output(void)
{
    bool was = playing;
    if (playing && hal_clock_milliseconds() - last_packet_ms > STREAM_IDLE_MS) {
        stop_output();
    } else if (!playing && receiving && usb_audio_sink_primed(&sink)) {
        playing = board_audio_stream_play();
        usb_audio_sink_set_playing(&sink, playing);
    }
    if (was != playing) {
        show_state();
    }
}

static void set_volume(uint8_t level)
{
    level = headphone_audio_limit(hal_audio_headphones(), level);
    shared->dac_volume = level;
    settings.volume = level;
    hal_audio_set_volume(level);
    usb_dac_menu_show_volume(&menu, level);
}

static void switch_headphones(void)
{
    HeadphoneProfiles chosen = *app_headphone_profiles();
    chosen.current = settings.headphones;
    HeadphoneAudio after = headphone_store_parameters(&chosen);
    set_volume(headphone_audio_switch_volume(hal_audio_headphones(), after, shared->dac_volume));
    hal_audio_set_headphones(after);
    usb_audio_sink_set_headphones(&sink, after);
}

static void step_volume(int step)
{
    int level = shared->dac_volume + step;
    if (level < 0 || level > HAL_AUDIO_VOLUME_MAX) {
        return;
    }
    set_volume((uint8_t)level);
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

static void apply_choice(UsbDacAction action)
{
    static const HalAudioOutput OUTPUTS[] = {HAL_AUDIO_OUTPUT_AUTO, HAL_AUDIO_OUTPUT_SINGLE_ENDED,
                                             HAL_AUDIO_OUTPUT_BALANCED};
    settings.gain = menu.settings.gain;
    settings.output = menu.settings.output;
    settings.headphones = menu.settings.headphones;
    if (action == USB_DAC_ACTION_HEADPHONES) {
        switch_headphones();
    } else if (action == USB_DAC_ACTION_VOLUME) {
        set_volume(menu.settings.volume);
    } else if (action == USB_DAC_ACTION_GAIN) {
        hal_audio_set_gain(settings.gain == GAIN_HIGH ? HAL_AUDIO_GAIN_HIGH : HAL_AUDIO_GAIN_LOW);
    } else if (action == USB_DAC_ACTION_OUTPUT) {
        hal_audio_set_output(OUTPUTS[settings.output % OUTPUT_JACK_COUNT]);
    }
}

static void follow_menu(UsbDacAction action)
{
    if (action == USB_DAC_ACTION_NONE) {
        return;
    }
    if (action == USB_DAC_ACTION_CLOSE) {
        menu_open = false;
    } else if (action == USB_DAC_ACTION_FILE_TRANSFER) {
        next = DAC_NEXT_STORAGE;
    } else if (action == USB_DAC_ACTION_CHARGE_ONLY) {
        next = DAC_NEXT_CHARGE;
    } else {
        apply_choice(action);
    }
    show_state();
}

static void open_menu(uint32_t now_ms)
{
    settings.volume = shared->dac_volume;
    usb_dac_menu_open(&menu, (UsbDacMenuConfig){app_theme(), settings, app_headphone_profiles()},
                      now_ms);
    menu_open = true;
    holding_volume = false;
    show_state();
}

static void follow_back(HalKeyTransition key)
{
    if (key.pressed) {
        back_armed = !menu_open;
        back_pressed_ms = key.time_ms;
        if (menu_open) {
            follow_menu(usb_dac_menu_key(&menu, KEY_BACK, key.time_ms));
        }
    } else if (back_armed && key.time_ms - back_pressed_ms >= BACK_HOLD_MS) {
        next = DAC_NEXT_MODES;
    }
}

static void follow_key(HalKeyTransition key)
{
    if (key.key == KEY_BACK) {
        follow_back(key);
    } else if (menu_open) {
        if (key.pressed) {
            follow_menu(usb_dac_menu_key(&menu, (Key)key.key, key.time_ms));
        }
    } else if (key.key == KEY_VOLUME_UP || key.key == KEY_VOLUME_DOWN) {
        follow_volume_key(key);
    } else if (key.key == KEY_MENU && key.pressed) {
        open_menu(key.time_ms);
    }
}

static void follow_keys(void)
{
    HalKeyTransition key;
    while (next == DAC_NEXT_STAY && hal_input_next_transition(&key)) {
        follow_key(key);
    }
}

static void refresh(uint32_t now_ms, uint32_t *refreshed)
{
    if (now_ms - *refreshed < INFO_REFRESH_MS || menu_open) {
        return;
    }
    *refreshed = now_ms;
    uint8_t shown = scene.state;
    follow_info(now_ms);
    AppUsbStatus status = dac_status();
    if (scene.state != shown) {
        app_show_usb_status(status);
        return;
    }
    UsbScreenLayout layout = usb_screen_layout(app_theme(), status);
    usb_screen_show_in_bands(status, usb_screen_column(&layout));
}

static void serve(void)
{
    back_armed = false;
    uint32_t refreshed = hal_clock_milliseconds();
    while (next == DAC_NEXT_STAY && usb_session_present(shared, &forced, received_packets)) {
        follow_keys();
        serve_usb();
        hal_audio_service();
        board_display_service();
        follow_host();
        keep_output();
        repeat_volume();
        uint32_t now = hal_clock_milliseconds();
        step_meter(now);
        refresh(now, &refreshed);
    }
}

static void start_session(void)
{
    uint32_t frames = 0;
    uint32_t *ring = board_audio_stream_ring(&frames);
    usb_audio_sink_init(&sink, ring, frames);
    usb_audio_sink_set_headphones(&sink, hal_audio_headphones());
    open_rate = 0;
    received_packets = 0;
    played_packets = 0;
    forced = (UsbForced){0};
    scene = (UsbScene){.state = USB_SCENE_DAC_CHOOSE};
    receiving = false;
    playing = false;
    holding_volume = false;
    menu_open = false;
    next = DAC_NEXT_STAY;
    settings = app_dac_settings();
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

static void remember(AppDacSettings before)
{
    settings.volume = shared->dac_volume;
    if (settings.volume != before.volume || settings.gain != before.gain ||
        settings.output != before.output || settings.headphones != before.headphones) {
        app_remember_dac(settings);
    }
}

static void switch_mode(void)
{
    shared->show_modes = next == DAC_NEXT_MODES;
    if (next == DAC_NEXT_STORAGE) {
        hal_system_request_usb_mode(HAL_USB_MODE_STORAGE);
    } else if (next == DAC_NEXT_CHARGE) {
        hal_system_request_usb_mode(HAL_USB_MODE_PLAYER);
    }
}

void usb_dac_session(UsbShared *state)
{
    shared = state;
    app_show_usb_status(
        (AppUsbStatus){.view = APP_USB_CONNECTING, .detail = i18n_text(STR_USB_STARTING)});
    uint8_t level = board_audio_volume_level();
    start_session();
    AppDacSettings before = settings;
    before.volume = shared->dac_volume;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        connected_ms = hal_clock_milliseconds();
        meter_ms = connected_ms;
        underruns_before = hal_audio_stats().output_underruns;
        show_state();
        serve();
    }
    receiving = false;
    stop_output();
    board_audio_stream_close();
    board_audio_set_attenuation(0);
    hal_audio_set_volume(level);
    usb_power_down();
    remember(before);
    switch_mode();
}
