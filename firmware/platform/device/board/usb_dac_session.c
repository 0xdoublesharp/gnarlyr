#include "usb_session.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/headphone_audio.h"
#include "core/i18n.h"
#include "debug_probe.h"
#include "device/dac_sequence.h"
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
    RATE_WINDOW_MS = 500,
    HINT_CAPACITY = 96,
    STEREO = 2,
    CAPTURE_DAC_SLOT = 3
};

typedef struct DacTally {
    uint32_t received_bytes;
    uint32_t counted_bytes;
    uint32_t counted_ms;
    uint32_t buffer_sum;
    uint32_t buffer_samples;
    uint16_t buffer_low;
    uint16_t buffer_high;
} DacTally;

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
#ifdef ECHO_DEBUG_LOOP
#define USB_DAC_RING_FRAMES 2200u
#else
#define USB_DAC_RING_FRAMES 3000u
#endif

_Static_assert(USB_DAC_RING_FRAMES > 192000 / 200 + 192000 / 1000 + 25,
               "the high-speed ring holds priming, a packet and silence");
_Static_assert(USB_DAC_RING_FRAMES > 48000 / 50 + 48000 / 250 + 49 + 48000 / 50,
               "the full-speed ring holds adaptive drift and silence");

_Static_assert(2 * USB_DAC_RING_FRAMES * sizeof(uint32_t) <= USB_SESSION_BUFFER_BYTES,
               "the DAC ring fits the idle mass-storage buffer");

static const char CAPTURE_DAC[] = "dac";
static DacTally tally;
static char hint[HINT_CAPACITY];
static int hinted_page;
static int hint_pages;
static UsbScene scene;
static UsbDacMenu menu;
static AppDacSettings settings;
static uint32_t open_rate;
static uint32_t last_packet_ms;
static uint32_t received_packets;
static uint32_t connected_ms;
static uint32_t meter_ms;
static uint32_t underruns_before;
static UsbForced forced;
static uint32_t back_pressed_ms;
static bool back_armed;
static bool menu_open;
static bool receiving;
static bool playing;
static DacNext next;

static void start_output(void)
{
    if (!playing && receiving && usb_audio_sink_primed(&sink)) {
        playing = true;
        usb_audio_sink_set_playing(&sink, true);
        playing = board_audio_stream_play();
        usb_audio_sink_set_playing(&sink, playing);
    }
}

static void receive_stream(const uint8_t *bytes, uint32_t length)
{
    received_packets++;
    tally.received_bytes += length;
    if (receiving) {
        last_packet_ms = hal_clock_milliseconds();
        usb_audio_sink_write(&sink, bytes, length, board_audio_stream_consumed());
    }
}

static uint32_t feedback(uint16_t tick)
{
    if (!playing || sink.rate != audio.rate) {
        return (uint32_t)(((uint64_t)audio.rate << 16) / 8000);
    }
    return usb_audio_sink_feedback(&sink, board_audio_stream_consumed(), tick);
}

static void serve_usb(void)
{
    for (int poll = 0; poll < POLLS_PER_SERVICE; poll++) {
        usb_poll(&port);
    }
}

static void serve_audio_usb(void)
{
    for (int poll = 0; poll < POLLS_PER_SERVICE; poll++) {
        usb_poll_audio(&port);
    }
}

static void keep_stream(void);

static void serve_drawing_usb(void)
{
    serve_audio_usb();
    keep_stream();
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
    if (sink.asynchronous && sink.feedback_started) {
        uint32_t nominal = (uint32_t)(((uint64_t)audio.rate << 16) / 8000);
        return (int32_t)(((int64_t)sink.feedback_rate - nominal) * PPM / nominal);
    }
    uint64_t frames = sink.stats.packets * (uint64_t)audio.rate /
                      (usb_audio_asynchronous(&audio) ? 8000 : MS_PER_SECOND);
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

static void follow_rate(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - tally.counted_ms;
    if (elapsed < RATE_WINDOW_MS) {
        return;
    }
    scene.dac.data_rate = (tally.received_bytes - tally.counted_bytes) / elapsed;
    tally.counted_bytes = tally.received_bytes;
    tally.counted_ms = now_ms;
}

static void follow_buffer(void)
{
    UsbSceneDac *dac = &scene.dac;
    if (tally.buffer_samples == 0) {
        return;
    }
    dac->buffer_low_ms = tally.buffer_low;
    dac->buffer_high_ms = tally.buffer_high;
    dac->buffer_average_ms = (uint16_t)(tally.buffer_sum / tally.buffer_samples);
}

static void follow_details(uint32_t now_ms)
{
    UsbSceneDac *dac = &scene.dac;
    dac->channels = STEREO;
    dac->alt = audio.alt;
    dac->asynchronous = usb_audio_asynchronous(&audio);
    dac->feedback = sink.asynchronous && sink.feedback_started ? sink.feedback_rate : 0;
    dac->overruns = sink.stats.overflows;
    dac->gain_high = settings.gain == GAIN_HIGH;
    dac->gain_register = dac->gain_high ? DAC_GAIN_HIGH_VALUE : DAC_GAIN_LOW_VALUE;
    follow_rate(now_ms);
    follow_buffer();
}

static void follow_info(uint32_t now_ms)
{
    UsbSceneDac *dac = &scene.dac;
    follow_details(now_ms);
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
    dac->underruns = hal_audio_stats().output_underruns - underruns_before + sink.stats.underruns;
    dac->seconds = (now_ms - connected_ms) / MS_PER_SECOND;
    const HeadphoneProfiles *profiles = app_headphone_profiles();
    dac->headphones =
        settings.headphones < profiles->count ? profiles->slots[settings.headphones].name : NULL;
}

static StringId back_action(int pages)
{
    if (scene.page + 1 >= pages) {
        return STR_USB_BACK_OVERVIEW;
    }
    return scene.page == USB_SCENE_OVERVIEW ? STR_USB_BACK_DETAILS : STR_USB_BACK_MORE;
}

static const char *paged_hint(void)
{
    if (hinted_page == scene.page + 1) {
        return hint_pages <= 1 ? i18n_text(STR_USB_DAC_HINT) : hint;
    }
    hinted_page = scene.page + 1;
    StringBuilder builder = string_builder_make(hint, sizeof hint);
    i18n_format_text(&builder, STR_USB_DAC_PAGE_HINT, i18n_text(STR_USB_BACK_OVERVIEW));
    AppUsbStatus probe = {.view = APP_USB_DAC, .detail = hint, .scene = &scene};
    const ListView *shown_menu = scene.menu;
    scene.menu = NULL;
    int pages = usb_screen_pages(app_theme(), probe);
    scene.menu = shown_menu;
    hint_pages = pages;
    if (pages <= 1) {
        return i18n_text(STR_USB_DAC_HINT);
    }
    builder = string_builder_make(hint, sizeof hint);
    i18n_format_text(&builder, STR_USB_DAC_PAGE_HINT, i18n_text(back_action(pages)));
    return hint;
}

static AppUsbStatus dac_status(void)
{
    scene.menu = menu_open ? &menu.view : NULL;
    const char *detail = paged_hint();
    if (menu_open) {
        detail = i18n_text(menu.adjusting ? STR_USB_DAC_ADJUST_HINT : STR_QUICK_SETTINGS_HINT);
    }
    return (AppUsbStatus){.view = APP_USB_DAC, .detail = detail, .scene = &scene};
}

static bool showing_details(void)
{
    dac_status();
    return !menu_open && scene.page != USB_SCENE_OVERVIEW && scene.page < hint_pages;
}

static Rect info_area(AppUsbStatus status)
{
    const Theme *theme = app_theme();
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    return showing_details() ? usb_screen_content(theme, &layout) : usb_screen_column(&layout);
}

static void show_state(void)
{
    follow_info(hal_clock_milliseconds());
    serve_audio_usb();
    AppUsbStatus status = dac_status();
    serve_audio_usb();
    if (playing && usb_audio_asynchronous(&audio)) {
        const Theme *theme = app_theme();
        usb_screen_show_in_bands(
            status, (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height});
    } else {
        app_show_usb_status(status);
    }
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

static void sample_buffer(void)
{
    if (!playing) {
        return;
    }
    uint16_t level = buffer_ms();
    bool first = tally.buffer_samples == 0;
    tally.buffer_low = first || level < tally.buffer_low ? level : tally.buffer_low;
    tally.buffer_high = first || level > tally.buffer_high ? level : tally.buffer_high;
    tally.buffer_sum += level;
    tally.buffer_samples++;
}

static void step_meter(uint32_t now_ms)
{
    if (menu_open || now_ms - meter_ms < METER_FRAME_MS) {
        return;
    }
    uint8_t changed = follow_levels(now_ms);
    meter_ms = now_ms;
    sample_buffer();
    if (scene.page != USB_SCENE_OVERVIEW) {
        return;
    }
    AppUsbStatus status = dac_status();
    const Theme *theme = app_theme();
    UsbScreenLayout layout = usb_screen_layout(theme, status);
    for (int side = 0; side < USB_SCENE_CHANNELS; side++) {
        if ((changed & (1u << side)) != 0) {
            usb_screen_show_in_bands(status, usb_screen_meter_bar(theme, &layout, side));
        }
    }
}

static void stop_output(void)
{
    if (playing) {
        board_audio_stream_pause();
        playing = false;
    }
    usb_audio_sink_set_asynchronous(&sink, usb_audio_asynchronous(&audio));
    usb_audio_sink_start(&sink, audio.rate, usb_audio_sample_bytes(&audio));
}

static void follow_format(void)
{
    receiving = false;
    stop_output();
    if (usb_audio_sample_bytes(&audio) != 0 && audio.rate != open_rate) {
        open_rate = board_audio_stream_open(audio.rate, serve_audio_usb) ? audio.rate : 0;
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

static void keep_stream(void)
{
    bool empty = playing && sink.asynchronous &&
                 (int32_t)(sink.written - board_audio_stream_consumed()) <= 0;
    if (playing && (empty || hal_clock_milliseconds() - last_packet_ms > STREAM_IDLE_MS)) {
        stop_output();
    } else {
        start_output();
    }
}

static void keep_output(void)
{
    keep_stream();
    if (scene.state != stream_state()) {
        follow_info(hal_clock_milliseconds());
        AppUsbStatus status = dac_status();
        usb_screen_show_in_bands(status, info_area(status));
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
    } else if (action == USB_DAC_ACTION_COMPUTER) {
        next = DAC_NEXT_STORAGE;
    } else if (action == USB_DAC_ACTION_CHARGE) {
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
    port.hid.held = 0;
    port.hid.skipped = 0;
    show_state();
}

static void next_page(void)
{
    dac_status();
    serve_audio_usb();
    scene.page = (uint8_t)((scene.page + 1) % (hint_pages > 0 ? hint_pages : 1));
    shared->usb_page = scene.page;
    follow_info(hal_clock_milliseconds());
    serve_audio_usb();
    AppUsbStatus status = dac_status();
    serve_audio_usb();
    const Theme *theme = app_theme();
    int top = theme->metrics.header_height;
    usb_screen_show_in_bands(
        status, (Rect){0, top, theme->metrics.screen_width, theme->metrics.screen_height - top});
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
    } else if (back_armed) {
        back_armed = false;
        next_page();
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
    } else if (key.key == KEY_MENU && key.pressed) {
        open_menu(key.time_ms);
    } else {
        int step = usb_audio_hid_key(&port.hid, key.key, key.pressed, key.time_ms);
        if (step != 0) {
            step_volume(step);
        }
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
    usb_screen_show_in_bands(status, info_area(status));
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
        if (!menu_open) {
            usb_audio_hid_tick(&port.hid, hal_clock_milliseconds());
        }
        uint32_t now = hal_clock_milliseconds();
        step_meter(now);
        refresh(now, &refreshed);
    }
}

static void start_session(void)
{
    board_audio_stream_buffer(usb_session_buffer(), USB_DAC_RING_FRAMES);
    uint32_t frames = 0;
    uint32_t *ring = board_audio_stream_ring(&frames);
    usb_audio_sink_init(&sink, ring, frames);
    usb_audio_sink_set_headphones(&sink, hal_audio_headphones());
    open_rate = 0;
    received_packets = 0;
    forced = (UsbForced){0};
    scene = (UsbScene){
        .state = USB_SCENE_DAC_CHOOSE, .page = shared->usb_page, .service = serve_drawing_usb};
    tally = (DacTally){0};
    hinted_page = 0;
    receiving = false;
    playing = false;
    port.hid.held = 0;
    port.hid.skipped = 0;
    menu_open = false;
    next = DAC_NEXT_STAY;
    settings = app_dac_settings();
    port = (UsbPort){.link = shared->link,
                     .bulk_packet = USB_PACKET_SIZE,
                     .audio = &audio,
                     .feedback = feedback,
                     .format_changed = follow_host,
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
        (AppUsbStatus){.view = APP_USB_CONNECTING, .detail = i18n_text(STR_PLUG_DAC_LINE)});
    uint8_t level = board_audio_volume_level();
    start_session();
    AppDacSettings before = settings;
    before.volume = shared->dac_volume;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        connected_ms = hal_clock_milliseconds();
        tally.counted_ms = connected_ms;
        meter_ms = connected_ms;
        underruns_before = hal_audio_stats().output_underruns;
        show_state();
        serve();
    }
    receiving = false;
    stop_output();
    board_audio_stream_close();
    menu_open = false;
    usb_screen_capture(dac_status(), &scene, CAPTURE_DAC, CAPTURE_DAC_SLOT);
    board_audio_set_attenuation(0);
    hal_audio_set_volume(level);
    usb_power_down();
    remember(before);
    switch_mode();
}
