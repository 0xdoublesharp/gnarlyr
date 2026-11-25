#include "app/app.h"
#include "core/seam.h"

#include <stddef.h>

#include "ab_repeat.h"
#include "app_internal.h"
#include "app_screens.h"
#include "bluetooth_link.h"
#include "board_layout.h"
#include "card_files.h"
#include "core/clock_face.h"
#include "core/deadline.h"
#include "core/frame_pacer.h"
#include "core/hash.h"
#include "core/i18n.h"
#include "core/key_chord.h"
#include "core/key_crossing.h"
#include "core/key_lock.h"
#include "core/key_tracker.h"
#include "core/low_battery.h"
#include "core/power_policy.h"
#include "core/string_builder.h"
#include "cover_cache.h"
#include "debug_loop_internal.h"
#include "debug_runner.h"
#include "debug_screenshot.h"
#include "default_theme.h"
#include "draw_profile.h"
#include "extension_entry.h"
#include "extra_font.h"
#include "favorites_store.h"
#include "frame_overlay.h"
#include "gfx/font_pack.h"
#include "gfx/frame_diff.h"
#include "gfx/strip_renderer.h"
#include "gfx/text.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/power.h"
#include "headphone_store.h"
#include "language.h"
#include "library_rescan.h"
#include "library_store.h"
#include "listening_effects.h"
#include "modules/modules.h"
#include "music_check.h"
#include "player.h"
#include "plug_modes.h"
#include "scan_guard.h"
#include "screens/charge_screen.h"
#include "settings/rocker.h"
#include "settings_choose.h"
#include "settings_store.h"
#include "standby.h"
#include "ui/brand_mark.h"
#include "ui/fade.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"
#include "ui/screen_stack.h"
#include "ui/slide.h"
#include "ui/status_bar.h"
#include "ui/toast.h"
#include "ui/wire_motion.h"
#include "wall_time.h"

enum {
    NOW_PLAYING_RETRY_MS = 2000,
    PASS_BUSY_MS = 1,
    PASS_SCREEN_ON_MS = 4,
    PASS_SCREEN_OFF_MS = 10,
    VIEW_STAMP_CHECK_MS = 50,
    VIEW_STAMP_POSITION_MS = 1000,
    VIEW_STAMP_MINUTE_S = 60,
    RESCAN_PROGRESS_MS = 250,
    COVERS_IDLE_MS = 3000,
    COVERS_PAUSED_MS = 60000,
    NOTICE_TEXT_CAPACITY = 64,
    NOTICE_MESSAGE_CAPACITY = 160,
    TOAST_TEXT_CAPACITY = 64,
    BATTERY_CHECK_MS = 250,
    CHARGE_LIT_MS = 5000
};

static const KeyConfig KEY_CONFIG = {
    .timing =
        {
            .long_press_ms = 600,
            .repeat_delay_ms = 400,
            .repeat_interval_ms = 120,
        },
    .behaviors =
        {
            [KEY_VOLUME_UP] = KEY_BEHAVIOR_REPEAT,
            [KEY_VOLUME_DOWN] = KEY_BEHAVIOR_REPEAT,
            [KEY_PLAY] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_MENU] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_BACK] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_POWER] = KEY_BEHAVIOR_TAP_OR_HOLD,
        },
    .long_press_ms =
        {
            [KEY_POWER] = 2000,
        },
};

typedef struct AppState {
    KeyTracker keys;
    FrameStats frame_stats;
    FrameStats update_stats;
    FrameStats draw_stats;
    FrameStats send_stats;
    uint32_t send_us;
    ScreenStack stack;
    LibraryStore library;
    Player player;
    ListeningStore listening;
    bool listening_loaded;
    SettingsStore settings;
    HeadphoneStore headphones;
    uint8_t headphones_chosen;
    Theme theme;
    ThemeRuntime themes;
    Orientation shown_orientation;
    bool rocker_swapped[KEY_COUNT];
    FavoritesStore favorites;
    SpokenStore spoken;
    CoverCache covers;
    PowerPolicy power;
    Standby standby;
    LowBattery low_battery;
    uint32_t battery_check_ms;
    union {
        struct {
            bool rescan_requested;
            bool rescan_full;
        };
        uint16_t rescan_flags;
    };
    bool usb_power;
    bool library_rebuilt;
    uint8_t rescan_stops;
    bool covers_pending;
    bool rescan_manual;
    uint16_t covers_album;
    bool language_requested;
    bool music_check_requested;
    bool music_unchecked;
    bool reload_pending;
    bool prepare_pending;
    size_t prepare_index;
    CardFilesReport prepare_report;
    bool light_after_frame;
    StringId goodbye_text;
    Toast toast;
    uint32_t toast_composed;
    uint32_t now_playing_magic;
    uint32_t now_playing_retry_ms;
    BluetoothLink bluetooth;
    uint32_t shown_refusals;
    StatusBarState status_bar;
    char battery_text[STATUS_BAR_PERCENT_CAPACITY];
    char clock_text[STATUS_BAR_CLOCK_CAPACITY];
    char volume_text[4];
    char position_text[24];
    AppUsbRenderer usb_renderer;
    MessageScreen notice;
    char notice_message[NOTICE_MESSAGE_CAPACITY];
    WireMotion motion;
    FrameDiff frame_diff;
    FramePacer pacer;
    bool render_urgent;
    bool render_wanted;
    bool view_stamp_known;
    uint32_t view_stamp;
    uint32_t view_position;
    uint32_t view_checked_ms;
    uint32_t marquee_due_ms;
    Rect bands[MARQUEE_BAND_COUNT];
    bool marquee_due;
    uint8_t band_mask;
    bool frame_sent;
    Fade splash;
    Fade goodbye;
    Slide slide;
    Screen *leaving;
    bool saying_goodbye;
    bool wire_fade;
    uint32_t wire_magic;
    uint32_t now_ms;
    AppOptions options;
    bool display_on;
    bool powered_off;
    bool charging;
    bool usb_chooser_charging;
    Screen *usb_chooser;
    uint32_t charge_lit_until_ms;
    KeyLock lock;
    KeyChord chord;
    KeyCrossing crossing;
    uint8_t raw_held;
    bool scrub_swallow;
    bool loader_keys;
    HalBattery charge_battery;
    HalAudioJack listener_jack;
    bool listener_bluetooth;
    bool volume_lowered;
    bool startup_pending;
    uint8_t lowered_volume;
    PlayerOutput lowered_output;
    AppScreens screens;
    HalFile extra_font;
} AppState;

static AppState app;

static const FadeTiming SPLASH_TIMINGS[] = {{.fade_ms = 300, .hold_ms = 200, .rising = true},
                                            {.fade_ms = WIRE_BOOT_OPEN_MS, .rising = true}};
static const uint32_t WORK_CLOCK_MHZ = 200;
static const uint32_t SCREEN_ON_CLOCK_MHZ = 96;
static const uint32_t IDLE_CLOCK_MHZ = 24;
static const FadeTiming GOODBYE_TIMINGS[] = {{.fade_ms = 600, .hold_ms = 400, .rising = false},
                                             {.fade_ms = WIRE_BOOT_CLOSE_MS, .rising = false}};
static _Alignas(uint32_t) Color strip_pixels[HAL_DISPLAY_WIDTH * BOARD_STRIP_HEIGHT];
static uint32_t row_hashes[HAL_DISPLAY_LONG_SIDE];
static Theme notice_theme;

static void render_when_due(uint32_t now_ms, uint32_t started_us);
static bool fade_running(void);

static Screen *top_screen(void)
{
    return screen_stack_top(&app.stack);
}

static Rect whole_screen(void)
{
    const ThemeMetrics *metrics = &app.theme.metrics;
    return (Rect){0, 0, metrics->screen_width, metrics->screen_height};
}

static void apply_orientation(Orientation orientation)
{
    app.shown_orientation = orientation;
    default_theme_set_orientation(&app.theme, orientation);
    notice_theme = NOTICE_THEME;
    default_theme_set_orientation(&notice_theme, orientation);
    hal_display_set_rotation((HalDisplayRotation)orientation);
    frame_diff_init(&app.frame_diff, row_hashes, app.theme.metrics.screen_height);
}

static Key oriented_key(HalKeyTransition transition)
{
    if (transition.pressed) {
        Orientation orientation = app.settings.settings.orientation;
        bool changes_volume = top_screen()->hold_volume_keys;
        app.rocker_swapped[transition.key] = app.display_on &&
                                             top_screen()->keys != SCREEN_KEYS_TEST &&
                                             (changes_volume ? rocker_volume_swapped_in(orientation)
                                                             : rocker_swapped_in(orientation));
    }
    return rocker_key(transition.key, app.rocker_swapped[transition.key]);
}

static void run_player_command(NowPlayingCommand command);
static void combo_screenshot(void);
static void use_screen_key_behaviors(void);

static Key scrub_key(const NowPlayingScrub *scrub)
{
    return scrub->back ? KEY_VOLUME_DOWN : KEY_VOLUME_UP;
}

static bool scrub_swallows(Key key, bool pressed, uint32_t time_ms)
{
    NowPlayingScreen *now_playing = &app.screens.now_playing;
    NowPlayingScrub *scrub = &now_playing->scrub;
    if (scrub->active && pressed) {
        if (key == KEY_POWER) {
            scrub->active = false;
        } else if (key >= KEY_PLAY) {
            app.scrub_swallow = true;
        } else if (scrub->releasing) {
            now_playing_scrub_commit(now_playing);
        }
    } else if (scrub->active && key == scrub_key(scrub)) {
        scrub->releasing = true;
        scrub->release_ms = time_ms;
    }
    return app.scrub_swallow && pressed && key != KEY_POWER;
}

static void pass_key(Key key, bool pressed, uint32_t time_ms, uint8_t chord)
{
    if (scrub_swallows(key, pressed, time_ms)) {
        return;
    }
    if (!pressed) {
        key_tracker_key_up(&app.keys, key, time_ms);
    } else if (chord != KEY_CHORD_NONE) {
        key_tracker_chord_down(&app.keys, key, time_ms, chord);
    } else {
        key_tracker_key_down(&app.keys, key, time_ms);
    }
}

static bool chord_screen(void)
{
    return !app.display_on || top_screen() == &app.screens.now_playing.screen ||
           top_screen()->keys == SCREEN_KEYS_TEST;
}

static void follow_scrub(uint32_t now_ms)
{
    NowPlayingScreen *now_playing = &app.screens.now_playing;
    NowPlayingScrub *scrub = &now_playing->scrub;
    if (!scrub->active && !app.scrub_swallow) {
        return;
    }
    bool shown = app.display_on && top_screen() == &now_playing->screen && !app.saying_goodbye &&
                 hal_power_cable() == scrub->cable;
    bool cancelling = (app.raw_held & (1u << KEY_PLAY | 1u << KEY_BACK)) != 0;
    if (scrub->active && scrub->releasing && shown && !cancelling) {
        now_playing_scrub_commit(now_playing);
    }
    bool waiting = scrub->releasing
                       ? !deadline_elapsed(now_ms, scrub->release_ms, NOW_PLAYING_SCRUB_CANCEL_MS)
                       : app.keys.states[scrub_key(scrub)].held;
    scrub->active = scrub->active && shown && waiting;
    app.scrub_swallow = app.scrub_swallow && (app.raw_held & ~(1u << KEY_POWER)) != 0;
}

static uint8_t pass_through_chord(KeyStep step)
{
    if (step.key == KEY_PLAY || step.key == KEY_MENU) {
        app.chord.allowed = !app.charging && chord_screen();
    }
    KeyChordAction action = key_chord_take(&app.chord, step.key, step.pressed, step.time_ms);
    uint8_t chord = action == KEY_CHORD_START ? app.chord.held : KEY_CHORD_NONE;
    if (chord != KEY_CHORD_NONE) {
        key_tracker_forget(&app.keys, chord == KEY_CHORD_MENU ? KEY_MENU : KEY_PLAY);
    }
    if (action != KEY_CHORD_DROP) {
        pass_key(step.key, step.pressed, step.time_ms, chord);
    }
    return chord;
}

static uint8_t pass_steps(const KeyCrossingOut *out)
{
    uint8_t chord = KEY_CHORD_NONE;
    for (int index = 0; index < out->count; index++) {
        uint8_t started = pass_through_chord(out->steps[index]);
        chord = started != KEY_CHORD_NONE ? started : chord;
    }
    return chord;
}

static uint8_t pass_through_crossing(Key key, HalKeyTransition transition)
{
    KeyCrossingRules rules = {.menu = !app.charging && chord_screen(),
                              .volume = app.chord.held == KEY_CHORD_MENU};
    KeyCrossingOut out = {0};
    key_crossing_take(&app.crossing, (KeyStep){key, transition.pressed, transition.time_ms}, rules,
                      &out);
    return pass_steps(&out);
}

static void settle_crossing(uint32_t now_ms)
{
    KeyCrossingOut out = {0};
    key_crossing_settle(&app.crossing, now_ms, &out);
    pass_steps(&out);
}

static void drop_seed_gestures(void)
{
    key_crossing_reset(&app.crossing);
    key_tracker_cancel(&app.keys, KEY_VOLUME_UP);
    key_tracker_cancel(&app.keys, KEY_VOLUME_DOWN);
    key_tracker_forget(&app.keys, KEY_MENU);
    key_tracker_forget(&app.keys, KEY_PLAY);
}

static void dispatch_transition(HalKeyTransition transition, Key key, bool scripted)
{
    KeyLockVerdict verdict =
        key_lock_transition(&app.lock, key, transition.pressed, transition.time_ms);
    uint8_t chord =
        verdict == KEY_LOCK_PASS ? pass_through_crossing(key, transition) : KEY_CHORD_NONE;
    if (!scripted && top_screen()->keys == SCREEN_KEYS_TEST) {
        debug_loop_ladder(transition, chord != KEY_CHORD_NONE);
    }
    switch (verdict) {
    case KEY_LOCK_PASS:
        break;
    case KEY_LOCK_COMBO_STARTED:
        drop_seed_gestures();
        key_tracker_cancel(&app.keys, KEY_POWER);
        break;
    case KEY_LOCK_SHORTCUT:
        drop_seed_gestures();
        key_tracker_cancel(&app.keys, KEY_POWER);
        key_tracker_cancel(&app.keys, key);
        if (!app.charging && !fade_running()) {
            if (key == KEY_MENU) {
                combo_screenshot();
            } else {
                run_player_command(key == KEY_VOLUME_UP ? NOW_PLAYING_NEXT : NOW_PLAYING_PREVIOUS);
            }
        }
        break;
    case KEY_LOCK_REFUSED:
        toast_show(&app.toast, i18n_text(STR_LOCKED_HINT), transition.time_ms);
        break;
    case KEY_LOCK_SWALLOW:
        break;
    }
}

enum {
    LOADER_KEYS = 1u << KEY_POWER | 1u << KEY_VOLUME_UP
};

static bool loader_gesture_key(Key key)
{
    return key == KEY_POWER || key == KEY_VOLUME_UP || key == KEY_VOLUME_DOWN;
}

static void start_loader_keys(void)
{
    key_tracker_cancel(&app.keys, KEY_POWER);
    key_tracker_cancel(&app.keys, KEY_VOLUME_UP);
    key_tracker_cancel(&app.keys, KEY_VOLUME_DOWN);
    key_lock_interrupt(&app.lock);
}

static bool follow_loader_keys(uint32_t time_ms)
{
    bool was = app.loader_keys;
    app.loader_keys = (app.raw_held & ~LOADER_KEYS) == 0 && hal_input_loader_keys_held();
    if (app.loader_keys && !was) {
        start_loader_keys();
    } else if (was && !app.loader_keys && (app.raw_held & 1u << KEY_POWER) != 0 &&
               key_lock_transition(&app.lock, KEY_POWER, true, time_ms) == KEY_LOCK_PASS) {
        key_tracker_resume(&app.keys, KEY_POWER, time_ms);
    }
    return was || app.loader_keys;
}

static bool loader_keys_take(HalKeyTransition transition, Key key)
{
    uint8_t bit = (uint8_t)(1u << transition.key);
    app.raw_held = transition.pressed ? app.raw_held | bit : app.raw_held & (uint8_t)~bit;
    return follow_loader_keys(transition.time_ms) && loader_gesture_key(key);
}

static void take_transition(HalKeyTransition transition, bool scripted)
{
    hal_system_coast(false);
    app.render_urgent = true;
    Key key = oriented_key(transition);
    if (!scripted && loader_keys_take(transition, key)) {
        debug_loop_ladder(transition, false);
        return;
    }
    dispatch_transition(transition, key, scripted);
}

static void read_input(void)
{
    HalKeyTransition transition;
    if (!app.loader_keys) {
        follow_loader_keys(hal_clock_milliseconds());
    }
    while (hal_input_next_transition(&transition)) {
        if (transition.pressed) {
            debug_runner_key_pressed();
        }
        take_transition(transition, false);
    }
    while (debug_runner_next_key(&transition)) {
        take_transition(transition, true);
    }
    settle_crossing(hal_clock_milliseconds());
}

static void use_wanted_clock(void)
{
    bool standby = app.power.standby_active;
    uint32_t wanted = !standby && app.display_on ? SCREEN_ON_CLOCK_MHZ : IDLE_CLOCK_MHZ;
    if (hal_system_clock_mhz() != wanted && (standby || hal_audio_status().idle)) {
        hal_system_set_clock_mhz(wanted);
    }
}

static void light_after_first_frame(void)
{
    if (app.light_after_frame) {
        app.light_after_frame = false;
        hal_display_set_power(true);
        debug_loop_boot(DEBUG_BOOT_FRAME);
        standby_frame(&app.standby);
    }
}

static void set_display(bool on)
{
    app.display_on = on;
    app.light_after_frame = on;
    if (on) {
        hal_system_coast(false);
        if (hal_system_clock_mhz() == SCREEN_ON_CLOCK_MHZ && !hal_audio_status().idle) {
            hal_system_set_clock_mhz(WORK_CLOCK_MHZ);
        }
    } else {
        hal_display_set_power(false);
    }
    use_wanted_clock();
    if (screen_stack_depth(&app.stack) > 0) {
        use_screen_key_behaviors();
    }
}

__attribute__((noinline)) static CORE_COLD void capture_settings(void)
{
    if (!app.prepare_pending || app.headphones.active.ceiling != 0) {
        settings_store_capture(&app.settings, player_status(&app.player));
    }
}

static void save_settings(void)
{
    capture_settings();
    settings_store_save(&app.settings);
}

static ListeningObservation listening_observation(void)
{
    ListeningOutput output =
        app.bluetooth.connected && !app.bluetooth.wired ? LISTENING_OUTPUT_BLUETOOTH
        : hal_audio_jack() == HAL_AUDIO_JACK_BALANCED   ? LISTENING_OUTPUT_BALANCED
                                                        : LISTENING_OUTPUT_SINGLE_ENDED;
    return (ListeningObservation){player_playback_status(&app.player), app.player.current_token,
                                  output};
}

static void flush_listening(void)
{
    if (app.listening_loaded) {
        listening_store_flush(&app.listening, listening_observation(), hal_clock_milliseconds());
    }
}

static bool mixtape_busy(void)
{
    return (app.screens.bound_modules & (1u << SCREEN_MODULE_VIEWS)) != 0 &&
           app.screens.modules.views.mixtapes.stage != 0;
}

static bool discovery_is_top(void)
{
    return (app.screens.bound_modules & (1u << SCREEN_MODULE_LIBRARY)) != 0 &&
           top_screen() == &app.screens.modules.library.discovery.screen;
}

static void cancel_discovery(void)
{
    DiscoveryScreen *screen = &app.screens.modules.library.discovery;
    if (!discovery_is_top() && screen->request == DISCOVERY_REQUEST_NONE) {
        return;
    }
    const LibraryModuleEntry *module =
        (const LibraryModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_LIBRARY);
    if (module != NULL && module->cancel != NULL) {
        module->cancel(&app.screens);
    }
}

static const Settings *preview_settings(void)
{
    if ((app.screens.bound_modules & (1u << SCREEN_MODULE_SETTINGS)) != 0) {
        SettingsScreens *screens = &app.screens.modules.settings.settings;
        if (top_screen() == &screens->choice.screen) {
            return screens->preview_settings(screens, true);
        }
    }
    return &app.settings.settings;
}

static void cancel_edits(void)
{
    screen_modules_cancel_previews(&app.screens);
    cancel_discovery();
    if (mixtape_busy()) {
        const ViewsModuleEntry *module =
            (const ViewsModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_VIEWS);
        if (module != NULL && module->cancel != NULL) {
            module->cancel(&app.screens.modules.views.mixtapes);
        }
    }
    player_queue_cancel(&app.player);
}

static void continue_mixtape(uint32_t now_ms)
{
    if (mixtape_busy() && top_screen() != &app.screens.modules.views.mixtapes.screen) {
        const ViewsModuleEntry *module =
            (const ViewsModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_VIEWS);
        if (module != NULL && module->step != NULL) {
            module->step(&app.screens.modules.views.mixtapes, now_ms);
        }
    }
}

__attribute__((noinline)) static CORE_COLD void flush_session_edits(void)
{
    cancel_edits();
    flush_listening();
    spoken_store_flush(&app.spoken, player_playback_status(&app.player), hal_clock_milliseconds());
}

static void save_session(void)
{
    flush_session_edits();
    player_save(&app.player);
    save_settings();
}

static void power_off_now(void)
{
    hal_audio_pause();
    set_display(false);
    app.powered_off = true;
    hal_power_off();
}

static void light_charge_screen(uint32_t now_ms)
{
    app.charge_lit_until_ms = now_ms + CHARGE_LIT_MS;
    app.render_wanted = true;
    if (!app.display_on) {
        set_display(true);
    }
}

static void start_charging(uint32_t now_ms)
{
    if (player_status(&app.player).state == PLAYER_PLAYING) {
        player_toggle_pause(&app.player);
    }
    player_release_output(&app.player);
    bluetooth_link_sleep(&app.bluetooth);
    app.charging = true;
    app.charge_battery = hal_power_battery();
    light_charge_screen(now_ms);
    debug_loop_save_report();
}

__attribute__((noinline)) static CORE_COLD void set_start_volume(PlayerOutput output,
                                                                 uint8_t volume)
{
    const Settings *settings = &app.settings.settings;
    uint8_t safe = output == PLAYER_OUTPUT_BLUETOOTH
                       ? settings_safe_start_bluetooth_volume(settings, volume)
                       : settings_safe_start_volume(settings, volume);
    if (safe < volume) {
        app.volume_lowered = true;
        app.lowered_volume = safe;
        app.lowered_output = output;
    }
    player_set_volume(&app.player, output, safe);
}

static void lower_start_volume(PlayerOutput output)
{
    set_start_volume(output, player_volume(&app.player, output));
}

static void note_listeners(void)
{
    app.listener_jack = hal_audio_jack();
    app.listener_bluetooth = hal_bluetooth_status()->state == HAL_BLUETOOTH_CONNECTED;
}

static void follow_listeners(void)
{
    HalAudioJack jack = hal_audio_jack();
    bool plugged = jack != HAL_AUDIO_JACK_NONE && jack != app.listener_jack;
    bool linked =
        hal_bluetooth_status()->state == HAL_BLUETOOTH_CONNECTED && !app.listener_bluetooth;
    note_listeners();
    if (plugged) {
        lower_start_volume(PLAYER_OUTPUT_WIRED);
    }
    if (linked) {
        lower_start_volume(PLAYER_OUTPUT_BLUETOOTH);
    }
}

static void explain_lowered_volume(uint32_t now_ms)
{
    static char text[TOAST_TEXT_CAPACITY];
    if (!app.volume_lowered || app.toast.visible || !app.display_on || fade_running()) {
        return;
    }
    app.volume_lowered = false;
    if (player_volume(&app.player, app.lowered_output) == app.lowered_volume) {
        StringBuilder builder = string_builder_make(text, sizeof text);
        i18n_format_counts(&builder, STR_VOLUME_LOWERED, app.lowered_volume, 0);
        toast_show(&app.toast, text, now_ms);
    }
}

static void stop_charging(uint32_t now_ms)
{
    lower_start_volume(PLAYER_OUTPUT_WIRED);
    bluetooth_link_start(&app.bluetooth, &app.toast, now_ms);
    power_policy_wake(&app.power, now_ms);
    if (!app.display_on) {
        set_display(true);
    }
    app_show_splash();
    app.charging = false;
}

static void finish_shut_down(void)
{
    app.saying_goodbye = false;
    if (hal_power_battery().usb_power) {
        start_charging(app.now_ms);
        return;
    }
    power_off_now();
}

static void start_fade(Fade *fade, const FadeTiming *timings, bool wire)
{
    const ScreenModuleEntry *entry = NULL;
    if (wire && app.settings.settings.startup_animation) {
        ScreenModule module = screen_modules_of(&app.screens, top_screen());
        entry = screen_modules_use(&app.screens, module == SCREEN_MODULE_PLAYING
                                                     ? module
                                                     : screen_modules_playing(&app.screens));
    }
    app.wire_fade = entry != NULL;
    app.wire_magic = entry != NULL ? entry->magic : 0;
    fade_start(fade, app.now_ms, timings[app.wire_fade]);
}

static void shut_down(StringId text)
{
    app.goodbye_text = text;
    save_session();
    if (!app.display_on) {
        finish_shut_down();
        return;
    }
    ScreenModule module = screen_modules_of(&app.screens, top_screen());
    bool shared = module == SCREEN_MODULE_NONE || module == SCREEN_MODULE_PLAYING ||
                  module == SCREEN_MODULE_STAGE;
    start_fade(&app.goodbye, GOODBYE_TIMINGS, text == STR_GOODBYE && shared);
    app.saying_goodbye = true;
}

static bool fade_running(void)
{
    return app.saying_goodbye || !fade_finished(&app.splash, app.now_ms);
}

static void drain_key_events(void)
{
    KeyEvent event;
    while (key_tracker_next_event(&app.keys, &event)) {
    }
}

static void set_key_behaviors(KeyBehavior volume, KeyBehavior seed)
{
    key_tracker_set_behavior(&app.keys, KEY_VOLUME_UP, volume);
    key_tracker_set_behavior(&app.keys, KEY_VOLUME_DOWN, volume);
    key_tracker_set_behavior(&app.keys, KEY_PLAY, seed);
    key_tracker_set_behavior(&app.keys, KEY_MENU, seed);
}

static void use_screen_key_behaviors(void)
{
    set_key_behaviors(KEY_BEHAVIOR_REPEAT,
                      chord_screen() ? KEY_BEHAVIOR_HOLD_AT_RELEASE : KEY_BEHAVIOR_TAP_OR_HOLD);
    key_tracker_follow_behavior(&app.keys, KEY_PLAY);
    key_tracker_follow_behavior(&app.keys, KEY_MENU);
    app.chord.allowed = !app.charging && chord_screen();
    if (app.display_on && !top_screen()->hold_volume_keys) {
        return;
    }
    const Settings *settings = &app.settings.settings;
    for (int index = 0; index < 2; index++) {
        int first = index == 0 ? PLAYBACK_VOLUME_UP_TAP : PLAYBACK_VOLUME_DOWN_TAP;
        if (settings->button_actions[first] != BUTTON_ACTION_DEFAULT ||
            settings->button_actions[first + 1] != BUTTON_ACTION_DEFAULT) {
            key_tracker_set_behavior(&app.keys, index == 0 ? KEY_VOLUME_UP : KEY_VOLUME_DOWN,
                                     KEY_BEHAVIOR_TAP_HOLD_REPEAT);
        }
    }
}

static void screen_changed(void)
{
    use_screen_key_behaviors();
    debug_loop_screen(top_screen()->title, screen_stack_depth(&app.stack));
}

static bool opens_now_playing(KeyEvent event)
{
    return event.key == KEY_MENU && event.gesture == KEY_GESTURE_PRESS &&
           player_status(&app.player).has_track &&
           top_screen() != &app.screens.now_playing.screen &&
           top_screen()->keys == SCREEN_KEYS_SHARED;
}

static ScreenAction now_playing_action(void)
{
    return top_screen() == &app.screens.modules.playing.up_next.screen ||
                   screen_stack_below_top(&app.stack) == &app.screens.now_playing.screen
               ? screen_action_pop()
               : screen_action_push(&app.screens.now_playing.screen);
}

static bool now_playing_ready(void)
{
    if (top_screen() == &app.screens.now_playing.screen && app.now_playing_magic == 0 &&
        app.now_playing_retry_ms != 0 && !deadline_reached(app.now_ms, app.now_playing_retry_ms)) {
        return true;
    }
    const PlayingModuleEntry *entry = (const PlayingModuleEntry *)screen_modules_use(
        &app.screens, screen_modules_playing(&app.screens));
    if (entry != NULL) {
        entry->now_playing_attach(&app.screens.now_playing);
        app.now_playing_magic = entry->module.magic;
    } else {
        now_playing_fallback_attach(&app.screens.now_playing);
        app.now_playing_magic = 0;
        app.now_playing_retry_ms = (app.now_ms + NOW_PLAYING_RETRY_MS) | 1u;
    }
    return true;
}

static bool now_playing_owned(void)
{
    const ScreenModuleEntry *loaded = hal_system_loaded_module();
    return loaded != NULL && loaded->magic == app.now_playing_magic;
}

static bool screen_ready(const Screen *screen)
{
    if (screen == &app.screens.now_playing.screen) {
        return now_playing_ready();
    }
    if (screen == &app.screens.home.screen) {
        screen_modules_prepare_home(&app.screens);
    }
    ScreenModule module = screen_modules_of(&app.screens, screen);
    return module == SCREEN_MODULE_NONE || screen_modules_use(&app.screens, module) != NULL;
}

static bool modules_agree(const Screen *first, const Screen *second)
{
    ScreenModule one = screen_modules_of(&app.screens, first);
    ScreenModule other = screen_modules_of(&app.screens, second);
    return one == SCREEN_MODULE_NONE || other == SCREEN_MODULE_NONE || one == other;
}

static void start_slide(Screen *leaving, int depth_before, uint32_t now_ms)
{
    if (top_screen() == leaving) {
        return;
    }
    app.slide.active = false;
    if (!modules_agree(leaving, top_screen())) {
        return;
    }
    bool deeper = screen_stack_depth(&app.stack) > depth_before;
    app.leaving = leaving;
    slide_start(&app.slide, now_ms, deeper ? SLIDE_FORWARD : SLIDE_BACK);
}

static bool slide_running(void)
{
    return app.slide.active && !slide_finished(&app.slide, app.now_ms);
}

static const SettingsModuleEntry *settings_module(void)
{
    return (const SettingsModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_SETTINGS);
}

static void explain_missing_screen(uint32_t now_ms)
{
    StringId text = hal_system_restart_pending() ? STR_RESTART_TO_FINISH : STR_SCREEN_MISSING;
    toast_show(&app.toast, i18n_text(text), now_ms);
}

static void go_home(uint32_t now_ms)
{
    cancel_edits();
    app.slide.active = false;
    if (screen_stack_apply(&app.stack, screen_action_pop_to_root(), now_ms)) {
        screen_changed();
    }
    screen_ready(top_screen());
}

static void settle_screens(uint32_t now_ms)
{
    if (!screen_ready(top_screen())) {
        explain_missing_screen(now_ms);
        go_home(now_ms);
    }
    if (slide_running() &&
        (!modules_agree(app.leaving, top_screen()) || !screen_ready(app.leaving))) {
        app.slide.active = false;
    }
}

static bool top_owned(uint32_t now_ms)
{
    Screen *top = top_screen();
    if (slide_running() || top != &app.screens.now_playing.screen || !now_playing_owned()) {
        settle_screens(now_ms);
    }
    return top_screen() == top;
}

static Screen *next_top(ScreenAction action)
{
    switch (action.type) {
    case SCREEN_ACTION_PUSH:
    case SCREEN_ACTION_REPLACE:
        return action.target;
    case SCREEN_ACTION_POP:
        return screen_stack_below_top(&app.stack);
    case SCREEN_ACTION_POP_TO_ROOT:
    case SCREEN_ACTION_NONE:
        break;
    }
    return NULL;
}

static bool apply_action(ScreenAction action, uint32_t now_ms)
{
    Screen *next = next_top(action);
    if (app.themes.previewing && action.type != SCREEN_ACTION_NONE) {
        screen_modules_cancel_previews(&app.screens);
    }
    EqualizerUi *equalizer = &app.screens.modules.equalizer;
    bool eq_screen = top_screen() == &equalizer->browser.choice.screen ||
                     top_screen() == &equalizer->graphic.screen ||
                     top_screen() == &equalizer->parametric.screen;
    if ((top_screen() == &app.screens.modules.settings.settings.choice.screen || eq_screen) &&
        (action.type == SCREEN_ACTION_POP || action.type == SCREEN_ACTION_POP_TO_ROOT ||
         (next != NULL && next != top_screen()))) {
        screen_modules_cancel_previews(&app.screens);
    }
    ScreenModule next_module =
        next != NULL ? screen_modules_of(&app.screens, next) : SCREEN_MODULE_NONE;
    if (mixtape_busy() && next_module != SCREEN_MODULE_NONE && next_module != SCREEN_MODULE_VIEWS) {
        cancel_edits();
    }
    if (discovery_is_top() &&
        ((next != NULL && next != &app.screens.modules.library.discovery.screen) ||
         action.type == SCREEN_ACTION_POP_TO_ROOT)) {
        cancel_discovery();
    }
    if (next == NULL || screen_ready(next)) {
        return screen_stack_apply(&app.stack, action, now_ms);
    }
    explain_missing_screen(now_ms);
    if (action.type == SCREEN_ACTION_POP) {
        go_home(now_ms);
    } else {
        settle_screens(now_ms);
    }
    return false;
}

static void dispatch_to_screen(KeyEvent event, uint32_t now_ms)
{
    if (!top_owned(now_ms)) {
        return;
    }
    Screen *screen = top_screen();
    int depth_before = screen_stack_depth(&app.stack);
    ScreenAction action = opens_now_playing(event)
                              ? now_playing_action()
                              : screen->handle_key(screen->context, event, now_ms);
    if (apply_action(action, now_ms)) {
        screen_changed();
        start_slide(screen, depth_before, now_ms);
    }
}

static bool may_show_now_playing(void)
{
    Screen *top = top_screen();
    return app.player.has_track && app.player.state == PLAYER_PLAYING &&
           top != &app.screens.now_playing.screen && !top->stays_on_wake;
}

static void show_now_playing(uint32_t now_ms)
{
    if (may_show_now_playing() &&
        apply_action(screen_action_push(&app.screens.now_playing.screen), now_ms)) {
        screen_changed();
    }
}

static void run_player_command(NowPlayingCommand command)
{
    if (mixtape_busy()) {
        cancel_edits();
    }
    now_playing_player_command(&app.screens.now_playing, command, app.now_ms);
}

static const NowPlayingCommand HEADSET_COMMANDS[] = {
    [HAL_HEADSET_TOGGLE_PAUSE] = NOW_PLAYING_TOGGLE_PAUSE,
    [HAL_HEADSET_NEXT] = NOW_PLAYING_NEXT,
    [HAL_HEADSET_PREVIOUS] = NOW_PLAYING_PREVIOUS,
    [HAL_HEADSET_VOLUME_UP] = NOW_PLAYING_VOLUME_UP,
    [HAL_HEADSET_VOLUME_DOWN] = NOW_PLAYING_VOLUME_DOWN,
};

static void read_headset(void)
{
    HalHeadsetCommand command;
    while (hal_input_next_headset_command(&command)) {
        if (!app.powered_off && !app.charging && !app.usb_chooser_charging &&
            !app.power.standby_active && !fade_running()) {
            run_player_command(HEADSET_COMMANDS[command]);
        }
    }
}

static bool ignored_power_tap(KeyEvent event)
{
    return app.options.ignore_power_taps && event.key == KEY_POWER &&
           event.gesture == KEY_GESTURE_PRESS;
}

static bool playback_button(KeyEvent event, PlaybackButton *button)
{
    int first;
    switch (event.key) {
    case KEY_VOLUME_UP:
        first = PLAYBACK_VOLUME_UP_TAP;
        break;
    case KEY_VOLUME_DOWN:
        first = PLAYBACK_VOLUME_DOWN_TAP;
        break;
    case KEY_PLAY:
        first = PLAYBACK_PLAY_TAP;
        break;
    case KEY_MENU:
        first = PLAYBACK_MENU_TAP;
        break;
    default:
        return false;
    }
    bool held = event.gesture == KEY_GESTURE_LONG_PRESS || event.gesture == KEY_GESTURE_REPEAT;
    if (!held && event.gesture != KEY_GESTURE_PRESS) {
        return false;
    }
    *button = (PlaybackButton)(first + (held ? 1 : 0));
    return true;
}

static CORE_COLD void wake_navigation(void)
{
    cancel_edits();
    app_wake();
    if (!app.display_on) {
        set_display(true);
    }
}

static void mapped_navigation(ButtonAction action, uint32_t now_ms)
{
    wake_navigation();
    if (action == BUTTON_ACTION_HOME) {
        go_home(now_ms);
    } else if (player_status(&app.player).has_track) {
        app_wake();
        Screen *screen = action == BUTTON_ACTION_UP_NEXT
                             ? &app.screens.modules.playing.up_next.screen
                             : &app.screens.now_playing.screen;
        if (top_screen() != screen && apply_action(screen_action_push(screen), now_ms)) {
            screen_changed();
        }
    }
}

static const uint8_t ACTION_COMMANDS[BUTTON_ACTION_COUNT] = {
    [BUTTON_ACTION_PLAY_PAUSE] = NOW_PLAYING_TOGGLE_PAUSE,
    [BUTTON_ACTION_NEXT] = NOW_PLAYING_NEXT,
    [BUTTON_ACTION_PREVIOUS] = NOW_PLAYING_PREVIOUS,
    [BUTTON_ACTION_VOLUME_UP] = NOW_PLAYING_VOLUME_UP,
    [BUTTON_ACTION_VOLUME_DOWN] = NOW_PLAYING_VOLUME_DOWN,
};

static bool mapped_playback(KeyEvent event, uint32_t now_ms)
{
    PlaybackButton button;
    if ((app.display_on && top_screen() != &app.screens.now_playing.screen) ||
        !playback_button(event, &button)) {
        return false;
    }
    ButtonAction action = settings_button_action(&app.settings.settings, button);
    if (action == BUTTON_ACTION_DEFAULT) {
        return false;
    }
    if (event.gesture == KEY_GESTURE_REPEAT && action != BUTTON_ACTION_VOLUME_UP &&
        action != BUTTON_ACTION_VOLUME_DOWN) {
        return true;
    }
    NowPlayingCommand command = (NowPlayingCommand)ACTION_COMMANDS[action];
    if (action == BUTTON_ACTION_AB_REPEAT) {
        ab_repeat_step(&app.player, &app.power, &app.toast, now_ms);
        app.screens.now_playing.repeat_feedback_ms = now_ms;
    } else if (command != NOW_PLAYING_NONE) {
        run_player_command(command);
    } else if (action == BUTTON_ACTION_UP_NEXT || action == BUTTON_ACTION_HOME) {
        mapped_navigation(action, now_ms);
    }
    return true;
}

static bool configured_menu_hold(KeyEvent event)
{
    if (top_screen()->keys != SCREEN_KEYS_SHARED) {
        return false;
    }
    if (!app_menu_hold(&app.screens, event)) {
        return false;
    }
    wake_navigation();
    return true;
}

static void dispatch_key_events(uint32_t now_ms)
{
    KeyEvent event;
    while (!app.powered_off && key_tracker_next_event(&app.keys, &event)) {
        app.render_urgent |=
            event.gesture != KEY_GESTURE_REPEAT || !app.screens.now_playing.scrub.active;
        debug_loop_key(event);
        if (top_screen()->keys == SCREEN_KEYS_TEST &&
            (event.key != KEY_POWER || event.gesture == KEY_GESTURE_PRESS)) {
            dispatch_to_screen(event, now_ms);
            continue;
        }
        if ((app.power.standby_active && event.key != KEY_POWER) || ignored_power_tap(event)) {
            continue;
        }
        PowerKeyOutcome outcome = power_policy_key(&app.power, event, now_ms);
        if (event.chord && !outcome.background && top_screen() != &app.screens.now_playing.screen) {
            continue;
        }
        if (outcome.shutdown) {
            debug_loop_shutdown(DEBUG_REPORT_SHUTDOWN);
            shut_down(STR_GOODBYE);
        } else if (event.key == KEY_BACK && event.gesture == KEY_GESTURE_LONG_PRESS) {
            wake_navigation();
            go_home(now_ms);
        } else if (!event.chord &&
                   (configured_menu_hold(event) || mapped_playback(event, now_ms))) {
            continue;
        } else if (outcome.background) {
            run_player_command(now_playing_command_for(event));
        } else if (!outcome.consumed) {
            dispatch_to_screen(event, now_ms);
        }
    }
}

static void announce_usb_power(bool usb_power, uint32_t now_ms)
{
    if (usb_power && !app.usb_power && app.settings.settings.battery_care == PLUG_CARE_USB) {
        toast_show(&app.toast, i18n_text(STR_PLUG_RUN_USB), now_ms);
    }
}

static bool battery_empty(uint32_t now_ms)
{
    if (!deadline_reached(now_ms, app.battery_check_ms)) {
        return false;
    }
    app.battery_check_ms = now_ms + BATTERY_CHECK_MS;
    HalBattery battery = hal_power_battery();
    announce_usb_power(battery.usb_power, now_ms);
    app.usb_power = battery.usb_power;
    LowBatteryReading reading = {
        .known = battery.known, .millivolts = battery.millivolts, .usb_power = battery.usb_power};
    return low_battery_update(&app.low_battery, reading, now_ms);
}

static void keep_awake_while_busy(uint32_t now_ms)
{
    if (app.usb_power || app.player.state == PLAYER_PLAYING || debug_runner_active() ||
        player_queue_busy(&app.player) || mixtape_busy()) {
        power_policy_stay_awake(&app.power, now_ms);
    }
}

static void follow_bluetooth_browsing(Screen *visible, uint32_t now_ms)
{
    if (visible == &app.screens.modules.settings.bluetooth.screen) {
        app.bluetooth.browsing = true;
    } else if (app.bluetooth.browsing) {
        app.bluetooth.browsing = false;
        app.bluetooth.search_now = false;
        app.bluetooth.unlinked_ms = now_ms;
    }
}

static void update_power(uint32_t now_ms)
{
    if (!app.saying_goodbye && battery_empty(now_ms)) {
        debug_loop_shutdown(DEBUG_REPORT_LOW_BATTERY);
        shut_down(STR_BATTERY_LOW);
        return;
    }
    keep_awake_while_busy(now_ms);
    uint32_t remaining = UINT32_MAX;
    if (app.power.sleep_timer_active) {
        if (app.power.sleep_track_end) {
            PlayerStatus status = player_status(&app.player);
            app.power.sleep_track_started |= status.state != PLAYER_IDLE;
            if (status.state == PLAYER_IDLE && app.power.sleep_track_started) {
                power_policy_start_sleep_timer(&app.power, 0, now_ms);
            } else if (status.state == PLAYER_PLAYING && status.duration_ms != 0) {
                remaining = status.position_ms < status.duration_ms
                                ? status.duration_ms - status.position_ms
                                : 0;
            }
        } else {
            remaining = deadline_reached(now_ms, app.power.sleep_deadline_ms)
                            ? 0
                            : app.power.sleep_deadline_ms - now_ms;
        }
    }
    if (!app.saying_goodbye) {
        hal_audio_set_sleep_remaining(remaining);
    }
    bool sleep_timer = app.power.sleep_timer_active;
    if (power_policy_update(&app.power, now_ms)) {
        bool slept = sleep_timer && !app.power.sleep_timer_active;
        debug_loop_shutdown(slept ? DEBUG_REPORT_SLEEP_TIMER : DEBUG_REPORT_AUTO_OFF);
        shut_down(STR_GOODBYE);
        return;
    }
    standby_follow(&app.standby, &app.power, &app.settings.settings);
    Screen *visible = app.power.display_on ? top_screen() : NULL;
    follow_bluetooth_browsing(visible, now_ms);
    if (app.power.display_on == app.display_on) {
#ifdef ECHO_DEBUG_LOOP
        if (visible == &app_screens_about(&app.screens)->diagnostics.screen) {
            return;
        }
#endif
        use_wanted_clock();
        return;
    }
    set_display(app.power.display_on);
    if (!app.display_on) {
        save_settings();
    } else {
        show_now_playing(now_ms);
    }
}

static void update_screen(uint32_t now_ms)
{
    top_owned(now_ms);
    Screen *screen = top_screen();
    if (screen->update != NULL) {
        screen->update(screen->context, now_ms);
    }
}

static const Theme *board_theme(void)
{
    return hal_system_restart_pending() ? &notice_theme : &app.theme;
}

const Theme *app_theme(void)
{
    return board_theme();
}

static bool draw_wire_fade(Canvas *canvas, const Fade *fade, const char *text)
{
    const PlayingModuleEntry *entry = hal_system_loaded_module();
    if (!app.wire_fade || entry == NULL || entry->module.magic != app.wire_magic) {
        return false;
    }
    entry->wire_boot(canvas, board_theme(), fade, text);
    return true;
}

static void draw_fade(Canvas *canvas, const Fade *fade, const char *text, bool art)
{
    if (draw_wire_fade(canvas, fade, text)) {
        return;
    }
    const Theme *theme = board_theme();
    Rect screen = whole_screen();
    uint8_t alpha = fade_alpha(fade, app.now_ms);
    Color background = theme->colors.background;
    TextStyle style = {.font = theme->fonts.title,
                       .color = color_blend(background, theme->colors.text, alpha)};
    Rect area = screen;
    bool painted = art && theme_art_draw(theme, canvas, THEME_ART_SPLASH, screen);
    if (!painted) {
        canvas_fill_rect(canvas, screen, background);
    }
    if (art && !painted) {
        SplashStack stack = board_splash_stack(theme, screen);
        brand_mark_draw(canvas, (BrandMark){stack.mark, background,
                                            (uint8_t)(alpha * UINT8_MAX / FADE_OPAQUE),
                                            brand_mark_needs_tile(background)});
        area = stack.label;
    }
    label_draw(canvas, style,
               (Label){.area = label_centered_area(style.font, area, text), .text = text});
}

static bool draw_running_fade(Canvas *canvas)
{
    if (app.saying_goodbye) {
        draw_fade(canvas, &app.goodbye, i18n_text(app.goodbye_text), false);
        return true;
    }
    if (!fade_finished(&app.splash, app.now_ms)) {
        draw_fade(canvas, &app.splash, board_splash_text(board_theme()), true);
        return true;
    }
    return false;
}

static void draw_shifted(Screen *screen, Canvas *canvas, int x, Rect area)
{
    Canvas shifted = canvas_shifted(canvas, x, area);
    if (!rect_is_empty(shifted.clip)) {
        screen->draw(screen->context, &shifted);
    }
}

static const Screen *shown_header_screen(const Screen *top)
{
    if (!slide_running() || slide_shows_arriving_title(&app.slide, app.now_ms)) {
        return top;
    }
    return app.leaving;
}

static const char *header_title_of(const Screen *screen)
{
    return screen->untitled_header ? NULL : screen->title;
}

static void draw_slide(Canvas *canvas, Screen *arriving)
{
    Screen *leaving = app.leaving;
    const Theme *theme = board_theme();
    bool whole = arriving->full_screen || leaving->full_screen;
    Rect area = whole ? whole_screen() : theme_content_area(theme);
    int width = app.theme.metrics.screen_width;
    draw_shifted(leaving, canvas, slide_leaving_x(&app.slide, app.now_ms, width), area);
    draw_shifted(arriving, canvas, slide_arriving_x(&app.slide, app.now_ms, width), area);
    if (!whole) {
        header_draw(canvas, theme, header_title_of(shown_header_screen(arriving)));
    }
}

static StatusBarState shown_status_bar(const AppState *state, const Screen *screen)
{
    StatusBarState bar = state->status_bar;
    const Screen *titled = shown_header_screen(screen);
    const char *title = header_title_of(titled);
    if (title != NULL) {
        bar.title_width = titled->header_title_width != 0
                              ? titled->header_title_width
                              : text_width(board_theme()->fonts.header, title);
    }
    if (screen == &app.screens.now_playing.screen) {
        bar.playback = STATUS_PLAYBACK_NONE;
    }
    bar.hides_charging = screen->hides_charging;
    if (screen->header_status != NULL) {
        bar.position = screen->header_status;
    }
    bar.logo = true;
    return bar;
}

static void publish_header_room(uint32_t now_ms)
{
    Screen *screen = top_screen();
    Theme *theme = hal_system_restart_pending() ? &notice_theme : &app.theme;
    StatusBarState bar = shown_status_bar(&app, screen);
    Rect area = status_bar_title_area(theme, &bar);
    area.height -= 1;
    int shift = 0;
    if (!slide_running() && !screen->full_screen && bar.title_width > area.width) {
        shift = marquee_offset_in(theme->marquee, bar.title_width, area,
                                  now_ms - app.power.last_activity_ms);
    }
    theme->header_room =
        (HeaderRoom){(int16_t)area.x, (int16_t)(area.x + area.width), (int16_t)shift};
}

static void draw_frame(void *context, Canvas *canvas)
{
    const AppState *state = context;
    if (draw_running_fade(canvas)) {
        return;
    }
    Screen *screen = screen_stack_top(&state->stack);
    if (slide_running()) {
        draw_slide(canvas, screen);
    } else {
        screen->draw(screen->context, canvas);
    }
    if (!screen->full_screen && canvas->clip.y < board_theme()->metrics.header_height) {
        status_bar_draw(canvas, board_theme(), shown_status_bar(state, screen));
    }
    toast_draw(&state->toast, canvas, board_theme());
    if (state->options.show_frame_time) {
        FrameBreakdown breakdown = {
            .total_us = frame_stats_average_us(&state->frame_stats),
            .update_us = frame_stats_average_us(&state->update_stats),
            .draw_us = frame_stats_average_us(&state->draw_stats),
            .send_us = frame_stats_average_us(&state->send_stats),
        };
        frame_overlay_draw(canvas, board_theme(), breakdown, hal_audio_stats());
    }
}

static void write_to_display(void *context, Rect area, const Color *pixels)
{
    (void)context;
    Rect changed;
    uint32_t audio_started_us = hal_clock_microseconds();
    hal_audio_service();
    hal_bluetooth_service();
    uint32_t hash_started_us = hal_clock_microseconds();
    draw_profile_add(DRAW_PART_AUDIO, hash_started_us - audio_started_us);
    bool any_changed = frame_diff_changed(&app.frame_diff, area, pixels, &changed);
    draw_profile_add(DRAW_PART_HASH, hal_clock_microseconds() - hash_started_us);
    if (!any_changed) {
        return;
    }
    app.frame_sent = true;
    const Color *first_row = pixels + (size_t)(changed.y - area.y) * (size_t)area.width;
    uint32_t started_us = hal_clock_microseconds();
    hal_display_write(changed.x, changed.y, changed.width, changed.height, first_row);
    app.send_us += hal_clock_microseconds() - started_us;
}

static StatusPlayback playback_of(PlayerState state)
{
    switch (state) {
    case PLAYER_PLAYING:
        return STATUS_PLAYBACK_PLAYING;
    case PLAYER_PAUSED:
        return STATUS_PLAYBACK_PAUSED;
    case PLAYER_IDLE:
        return STATUS_PLAYBACK_NONE;
    }
    return STATUS_PLAYBACK_NONE;
}

static StatusBluetooth bluetooth_of(const BluetoothLink *link)
{
    if (!link->saved.enabled || link->dozing) {
        return STATUS_BLUETOOTH_OFF;
    }
    return link->connected ? STATUS_BLUETOOTH_CONNECTED : STATUS_BLUETOOTH_ON;
}

static const char *battery_text(HalBattery battery)
{
    if (!app.settings.settings.battery_percent) {
        return NULL;
    }
    charge_screen_level_text(battery, app.battery_text, sizeof app.battery_text);
    return app.battery_text;
}

static const char *clock_text(void)
{
    WallNow now = wall_time_now();
    if (!now.known) {
        return NULL;
    }
    StringBuilder builder = string_builder_make(app.clock_text, sizeof app.clock_text);
    clock_face_text(&builder, clock_face(now.local_s, wall_time_24h(&app.settings.settings)));
    return app.clock_text;
}

static StatusBarState current_status_bar(void)
{
    HalBattery battery = hal_power_battery();
    PlayerStatus status = player_status(&app.player);
    uint8_t items = app.settings.settings.status_items;
    StringBuilder volume = string_builder_make(app.volume_text, sizeof app.volume_text);
    string_builder_append_unsigned(&volume, status.volume);
    StringBuilder position = string_builder_make(app.position_text, sizeof app.position_text);
    status_bar_position_text(&position,
                             (StatusQueuePosition){status.place.number, status.place.total, NULL});
    return (StatusBarState){
        .playback = (uint8_t)playback_of(status.state),
        .bluetooth = (uint8_t)bluetooth_of(&app.bluetooth),
        .battery_known = battery.known,
        .battery_percent = battery.percent,
        .usb_power = battery.usb_power,
        .charging = battery.charging,
        .charging_off = battery.charge_disabled,
        .locked = app.lock.locked,
        .shuffle = (uint8_t)status.shuffle,
        .percent = battery_text(battery),
        .clock = clock_text(),
        .volume = (items & SETTINGS_STATUS_VOLUME) != 0 ? app.volume_text : NULL,
        .position = (items & SETTINGS_STATUS_POSITION) != 0 && status.place.total > 0
                        ? app.position_text
                        : NULL,
        .repeat = (items & SETTINGS_STATUS_REPEAT) != 0 ? (uint8_t)status.repeat : 0,
        .segmented = (items & SETTINGS_STATUS_SEGMENTED) != 0,
    };
}

static StripRenderer frame_renderer(Rect area, bool drawer_paints_everything)
{
    return (StripRenderer){
        .strip_pixels = strip_pixels,
        .area = area,
        .strip_height = (int)(sizeof strip_pixels / sizeof strip_pixels[0]) / area.width,
        .background = BOARD_THEME.colors.background,
        .drawer_paints_everything = drawer_paints_everything,
    };
}

static void render(FrameDrawer drawer, bool drawer_paints_everything)
{
    StripRenderer renderer = frame_renderer(whole_screen(), drawer_paints_everything);
    FrameSink sink = {.write = write_to_display, .context = NULL};
    for (int band = 0; band <= MARQUEE_BAND_COUNT; band++) {
        bool full = band == MARQUEE_BAND_COUNT;
        if (full ? app.band_mask == 0 : (app.band_mask >> band & 1u) != 0) {
            if (!full) {
                renderer.area.y = app.bands[band].y;
                renderer.area.height = app.bands[band].height;
            }
            strip_renderer_render(&renderer, drawer, sink);
        }
    }
    hal_display_present();
}

static void send_motion(void *context, Rect area, const Color *pixels)
{
    (void)context;
    hal_audio_service();
    hal_bluetooth_service();
    hal_display_write(area.x, area.y, area.width, area.height, pixels);
}

static void render_area(Rect area, FrameDrawer drawer)
{
    StripRenderer renderer = frame_renderer(area, false);
    strip_renderer_render(&renderer, drawer, (FrameSink){.write = send_motion, .context = NULL});
    hal_display_present();
}

static bool same_area(Rect first, Rect second)
{
    return first.x == second.x && first.y == second.y && first.width == second.width &&
           first.height == second.height;
}

static void mask_motion(const void *owner)
{
    Rect area = wire_motion_shown(&app.motion, owner) ? app.motion.area : (Rect){0, 0, 0, 0};
    if (!same_area(area, app.frame_diff.masked)) {
        frame_diff_mask(&app.frame_diff, area);
        app.render_wanted = true;
    }
}

static void compose_toast(void)
{
    uint32_t shown = app.toast.visible ? app.toast.until_ms | 1u : 0;
    if (shown != app.toast_composed) {
        app.toast_composed = shown;
        frame_diff_mask(&app.frame_diff, app.frame_diff.masked);
    }
}

static void animate(FrameDrawer drawer, const void *owner, bool visible, uint32_t now_ms)
{
    NowPlayingScreen *playing = &app.screens.now_playing;
    if (owner == &playing->screen &&
        settings_now_playing_style(&app.settings.settings) != NOW_PLAYING_COVER &&
        now_playing_owned() && playing->config.cassette_animate != NULL) {
        app.render_wanted |= playing->config.cassette_animate(playing, &app.frame_diff, visible);
        return;
    }
    bool due = visible && app.motion.owner == owner && wire_motion_due(&app.motion, now_ms);
    if (due) {
        wire_motion_tick(&app.motion, now_ms);
    }
    mask_motion(visible ? owner : NULL);
    if (!due) {
        return;
    }
    if (rect_is_empty(app.motion.area)) {
        app.render_wanted = true;
    } else {
        render_area(app.motion.area, drawer);
    }
}

static void render_frame(void)
{
    uint32_t started_us = hal_clock_microseconds();
    app.send_us = 0;
    app.status_bar = current_status_bar();
    publish_header_room(app.now_ms);
    bool screen_paints = top_screen()->paints_every_pixel && !fade_running() && !slide_running();
    render((FrameDrawer){.draw = draw_frame, .context = &app}, screen_paints);
    uint32_t render_us = hal_clock_microseconds() - started_us;
    frame_stats_record(&app.send_stats, app.send_us);
    frame_stats_record(&app.draw_stats, render_us - app.send_us);
    draw_profile_finish_frame();
}

static void draw_notice(void *context, Canvas *canvas)
{
    Screen *screen = context;
    screen->draw(screen->context, canvas);
}

static void animate_notice(void)
{
    FrameDrawer drawer = {.draw = draw_notice, .context = &app.notice.screen};
    animate(drawer, &app.notice, app.display_on, hal_clock_milliseconds());
}

static void show_working_notice(StringId title, const char *message, WireShape shape)
{
    wire_motion_run(&app.motion, &app.notice, shape, hal_clock_milliseconds());
    app_show_notice(i18n_text(title), message);
}

static void start_playback(bool cold_start)
{
    flush_listening();
    app.listening_loaded = false;
    library_store_open(&app.library);
    player_init(&app.player, &app.library.library);
    settings_store_restore_playback(&app.settings, &app.player, hal_clock_microseconds());
    player_restore(&app.player, cold_start);
    player_set_volume(&app.player, PLAYER_OUTPUT_WIRED, app.settings.settings.volume);
    favorites_store_load(&app.favorites, &app.library.library);
    spoken_store_load(&app.spoken, &app.library.library);
    player_set_resume(&app.player, spoken_store_resume(&app.spoken));
    cover_cache_open(&app.covers, &app.library.library);
}

static void follow_appearance(void)
{
    const Settings *shown = preview_settings();
    if (shown->orientation != app.shown_orientation) {
        apply_orientation(shown->orientation);
    }
    AppearanceTheme appearance = app.themes.stored || app.themes.previewing
                                     ? (AppearanceTheme)app.themes.shown_base
                                     : shown->appearance;
    default_theme_set_appearance(&app.theme, appearance);
    default_theme_set_accent(&app.theme, shown->accent);
    theme_runtime_apply(&app.themes, &app.theme);
    notice_theme.colors = app.theme.colors;
    notice_theme.horizon = app.theme.horizon;
    notice_theme.header_rule = app.theme.header_rule;
    default_theme_set_text_size(&app.theme, shown->text_size);
    default_theme_set_status_bar(&app.theme, shown->status_bar_compact);
}

static void start_hardware(uint32_t now_ms)
{
    power_policy_init(&app.power, settings_screen_timeout_ms(&app.settings.settings), now_ms);
    power_policy_set_auto_off(&app.power, settings_auto_off_ms(&app.settings.settings), now_ms);
    settings_store_apply_hardware(&app.settings.settings);
    set_display(true);
}

static void open_start_screen(uint32_t start, uint32_t now_ms)
{
    Screen *path[START_PATH_CAPACITY];
    int count = app_screens_start_path(&app.screens, (StartScreen)start, path);
    for (int index = 0; index < count; index++) {
        apply_action(screen_action_push(path[index]), now_ms);
    }
}

static void build_screens(uint32_t now_ms)
{
    AppScreensContext context = {
        .options = &app.options,
        .theme = board_theme(),
        .styled_theme = &app.theme,
        .themes = &app.themes,
        .store = &app.library,
        .player = &app.player,
        .spoken = &app.spoken,
        .settings = &app.settings,
        .headphones = &app.headphones,
        .favorites = &app.favorites,
        .power = &app.power,
        .toast = &app.toast,
        .covers = &app.covers,
        .rescan_requested = &app.rescan_full,
        .language_requested = &app.language_requested,
        .bluetooth = &app.bluetooth,
        .listening = &app.listening,
        .motion = &app.motion,
    };
    Screen *root = app_screens_init(&app.screens, context);
    app.slide.active = false;
    screen_stack_init(&app.stack, root, now_ms);
}

static void start_screens(uint32_t now_ms)
{
    build_screens(now_ms);
    open_start_screen(app.settings.settings.start_screen, now_ms);
    screen_changed();
}

static void change_language(uint32_t now_ms)
{
    Settings *settings = &app.settings.settings;
    language_load(settings->language);
    app.toast.visible = false;
    build_screens(now_ms);
    const SettingsModuleEntry *module = settings_module();
    if (module == NULL) {
        explain_missing_screen(now_ms);
    } else {
        apply_action(screen_action_push(module->language_row(&app.screens)), now_ms);
    }
    use_screen_key_behaviors();
    if (!language_is(settings->language)) {
        StringBuilder code = string_builder_make(settings->language, sizeof settings->language);
        string_builder_append(&code, i18n_code());
        save_settings();
        toast_show(&app.toast, i18n_text(STR_LANGUAGE_FAILED), now_ms);
    }
}

CORE_COLD static bool library_refused(void)
{
    HalFile file;
    uint8_t magic[4] = {0, 0, 0, 0};
    if (app.library.available || app.library_rebuilt || !hal_file_open(&file, LIBRARY_STORE_PATH)) {
        return false;
    }
    bool read = hal_file_read(file, 0, magic, sizeof magic);
    hal_file_close(&file);
    return read && (magic[0] | magic[1] | magic[2] | magic[3]) != 0;
}

CORE_COLD static void request_start_scan(bool library_created)
{
    app.covers_pending = music_check_covers_pending();
    uint32_t stopped = music_check_interrupted_scans();
    bool resume = stopped > 0 && stopped <= SCAN_GUARD_RETRIES;
    if (library_created || resume || (stopped == 0 && library_refused())) {
        app.rescan_requested = true;
        app.rescan_stops = resume ? (uint8_t)stopped : 0;
    } else if (stopped == 0 && !app.library_rebuilt && music_check_firmware_is_new()) {
        app.covers_pending = true;
        app.music_check_requested = true;
    } else if (stopped == 0 && !app.library_rebuilt && music_check_generation_moved()) {
        app.music_check_requested = true;
    }
}

CORE_COLD static void report_card_files(CardFilesReport files, uint32_t now_ms)
{
    if (files.headphones_created && app.headphones.load == STORE_UNREADABLE) {
        app.headphones.file.needs_reload = false;
        app.headphones.load = STORE_EMPTY;
        app.headphones.active = headphone_audio_defaults();
        hal_audio_set_headphones(app.headphones.active);
        set_start_volume(PLAYER_OUTPUT_WIRED, app.settings.settings.volume);
    }
    if (files.card_full) {
        toast_show(&app.toast, i18n_text(STR_STORAGE_FULL), now_ms);
        debug_loop_event(DEBUG_EVENT_STORAGE, DEBUG_STORAGE_FULL);
    }
    uint8_t update;
    if (!hal_internal_file_read("/" FIRMWARE_UPDATE_FILE, &update, sizeof update)) {
        request_start_scan(files.library_created);
    }
}

static void start_app(bool power_on)
{
    extra_font_open(&app.extra_font);
#ifdef ECHO_USB_OVERLAY_RENDERER
    app.usb_renderer = NULL;
#else
    app.usb_renderer = usb_screen_draw;
#endif
    uint32_t now_ms = hal_clock_milliseconds();
    key_tracker_init(&app.keys, &KEY_CONFIG);
    frame_stats_reset(&app.frame_stats);
    frame_stats_reset(&app.update_stats);
    frame_stats_reset(&app.draw_stats);
    frame_stats_reset(&app.send_stats);
    app.pacer = (FramePacer){0};
    app.standby = (Standby){0};
    app.options = (AppOptions){.show_frame_time = false, .ignore_power_taps = false};
    app.toast = (Toast){.visible = false};
    app.powered_off = false;
    app.charging = false;
    app.usb_chooser_charging = false;
    app.usb_chooser = NULL;
    app.saying_goodbye = false;
    app.goodbye_text = STR_GOODBYE;
    low_battery_init(&app.low_battery);
    app.battery_check_ms = now_ms;
    app.splash = (Fade){.active = false};
    app.now_ms = now_ms;
    app.prepare_pending = true;
    app.startup_pending = power_on;
    app.prepare_index = 0;
    app.prepare_report = (CardFilesReport){0};
    (void)hal_system_settle_user_state(false);
    settings_store_load(&app.settings);
    language_load(app.settings.settings.language);
    headphone_store_load(&app.headphones);
    app.theme = BOARD_THEME;
    theme_runtime_close(&app.themes);
    app.themes = (ThemeRuntime){.file = {.handle = -1}};
    start_playback(power_on);
    uint8_t selection;
    if (hal_internal_file_read(THEME_SELECTION_PATH, &selection, 1)) {
        const ViewsModuleEntry *themes = hal_system_load_module(SCREEN_MODULE_VIEWS);
        if (themes != NULL) {
            themes->theme_boot(&app.themes);
        }
    }
    app.screens.bound_modules = 0;
    apply_orientation(app.settings.settings.orientation);
    follow_appearance();
    start_hardware(now_ms);
    if (!power_on) {
        bluetooth_link_resume(&app.bluetooth, &app.toast, now_ms);
    }
    start_screens(now_ms);
    note_listeners();
    app.volume_lowered = false;
    if (hal_power_take_usb_start()) {
        start_charging(now_ms);
    } else if (power_on) {
        lower_start_volume(PLAYER_OUTPUT_WIRED);
    }
}

int16_t app_clock_offset(void)
{
    return app.settings.settings.clock_offset_minutes;
}

CORE_SEAM CORE_COLD bool app_store_clock_offset(int16_t offset, bool from_storage)
{
    if (from_storage) {
        settings_store_load(&app.settings);
    } else {
        capture_settings();
    }
    bool saved = settings_store_set_clock_offset(&app.settings, offset);
    if (!saved) {
        toast_show(&app.toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
    return saved;
}

CORE_SEAM void app_start(void)
{
    app.library_rebuilt = false;
    start_app(true);
    app.listening.boot = (ListeningBoot){.pending = true};
}

CORE_SEAM void app_start_after_storage(void)
{
    start_app(false);
}

void app_reset_for_test(void)
{
    font_pack_detach_extra();
    app = (AppState){0};
}

void app_release_usb_glyphs(void)
{
    if (!hal_system_restart_pending()) {
        font_pack_hold(FONT_PACK_HOLD_NONE);
    }
}

static void show_rescan_count(bool first, StringId pattern, uint32_t count)
{
    static uint32_t shown_ms;
    uint32_t now_ms = hal_clock_milliseconds();
    hal_bluetooth_service();
    if (!first && !deadline_elapsed(now_ms, shown_ms, RESCAN_PROGRESS_MS)) {
        animate_notice();
        return;
    }
    shown_ms = now_ms;
    char text[NOTICE_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, pattern, count, 0);
    app_show_notice(i18n_text(STR_SCANNING), text);
}

static void show_rescan_progress(uint32_t files, uint32_t tracks)
{
    show_rescan_count(files <= 1, STR_SCAN_TRACKS, tracks);
}

static void show_build_progress(uint32_t phase, uint32_t percent)
{
    static const StringId PHASE_TEXT[] = {STR_SCAN_SORTING, STR_SCAN_PLAYLISTS, STR_SCAN_SAVING};
    show_rescan_count(percent == 0, PHASE_TEXT[phase % 3u], percent);
}

static void explain_refusals(uint32_t now_ms)
{
    static char text[TOAST_TEXT_CAPACITY];
    uint32_t count = app.player.refusal_count;
    bool fresh = count > app.shown_refusals;
    app.shown_refusals = count;
    if (fresh) {
        player_refusal_text(app.player.refusal, text, sizeof text);
        toast_show(&app.toast, text, now_ms);
    }
}

CORE_COLD static bool walk_music(uint32_t files)
{
    const ParametricModuleEntry *module =
        (const ParametricModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_EQUALIZER);
    app.music_unchecked = module == NULL;
    return module != NULL && module->music_walk(files);
}

__attribute__((noinline)) static void log_scan(DebugScanPhase phase, uint32_t number)
{
    debug_loop_event(DEBUG_EVENT_SCAN,
                     (uint32_t)phase << DEBUG_SCAN_PHASE_SHIFT | (number & DEBUG_SCAN_NUMBER_MASK));
}

static void continue_covers(uint32_t now_ms)
{
    bool idle = deadline_elapsed(now_ms, app.power.last_activity_ms, COVERS_IDLE_MS);
    bool rested = !app.player.loaded ||
                  (app.player.state == PLAYER_PAUSED &&
                   deadline_elapsed(now_ms, app.player.paused_since_ms, COVERS_PAUSED_MS));
    if (!app.covers_pending || !idle || !rested || debug_runner_active() || hal_input_waiting() ||
        hal_system_usb_connect_pending()) {
        return;
    }
    if (app.player.loaded) {
        app_release_audio();
    }
    hal_system_set_clock_mhz(WORK_CLOCK_MHZ);
    log_scan(DEBUG_SCAN_COVERS, app.covers_album);
    RescanOutcome outcome = library_rescan_covers(app.covers_album);
    use_wanted_clock();
    app.covers_album = (uint16_t)outcome.covers.next_album;
    bool stopped = outcome.cover_result == SCAN_STOPPED;
    app.covers_pending = stopped;
    log_scan(stopped ? DEBUG_SCAN_COVERS_STOPPED : DEBUG_SCAN_COVERS_DONE,
             stopped ? app.covers_album : outcome.covers.covers);
    cover_cache_open(&app.covers, &app.library.library);
    settle_screens(now_ms);
    app.render_wanted = true;
}

CORE_COLD static void run_rescan(void)
{
    bool loaded = app.player.loaded;
    app_release_audio();
    if (!loaded) {
        player_save(&app.player);
    }
    hal_audio_release();
    show_working_notice(STR_SCANNING, i18n_text(STR_SCAN_STARTING), WIRE_SHAPE_DISC);
    RescanProgress progress = {show_rescan_progress, show_build_progress};
    log_scan(DEBUG_SCAN_START, 0);
    hal_system_set_clock_mhz(WORK_CLOCK_MHZ);
    uint32_t stops = app.rescan_stops | (app.rescan_full ? SCAN_FULL_REQUEST : 0);
    app.rescan_stops = 0;
    app.rescan_flags = 0;
    RescanOutcome outcome = library_rescan_run(progress, stops);
    use_wanted_clock();
    static char message[TOAST_TEXT_CAPACITY];
    const char *text = library_rescan_message(outcome, message, sizeof message);
    if (outcome.result == SCAN_OK) {
        (void)walk_music(outcome.stats.files);
        app.library_rebuilt = true;
        bool charging = app.charging;
        start_app(false);
        if (charging) {
            start_charging(hal_clock_milliseconds());
        }
        app.covers_pending = true;
        app.covers_album = 0;
        log_scan(DEBUG_SCAN_LIBRARY, outcome.stats.tracks);
    } else {
        debug_loop_event(DEBUG_EVENT_STORAGE, DEBUG_STORAGE_SCAN_FAILED + (uint32_t)outcome.result);
    }
    app_wake();
    toast_show(&app.toast, text, hal_clock_milliseconds());
}

void app_leave_storage(void)
{
    extra_font_close(&app.extra_font);
    flush_listening();
    listening_store_detach(&app.listening);
    app.listening_loaded = false;
}

void app_release_audio(void)
{
    flush_session_edits();
    save_settings();
    player_release_output(&app.player);
}

void app_bluetooth_off(void)
{
    bluetooth_link_power_off(&app.bluetooth);
}

void app_reload(StringId toast)
{
    app.reload_pending = true;
    toast_show(&app.toast, i18n_text(toast), hal_clock_milliseconds());
    app.listening_loaded = false;
}

AppDacSettings app_dac_settings(void)
{
    const Settings *settings = &app.settings.settings;
    return (AppDacSettings){settings->dac_volume, (uint8_t)settings->gain,
                            (uint8_t)settings->output, app.headphones.profiles.current,
                            app.headphones.file.needs_reload};
}

const HeadphoneProfiles *app_headphone_profiles(void)
{
    return &app.headphones.profiles;
}

void app_remember_dac(AppDacSettings settings)
{
    if (settings.gain == GAIN_HIGH && app.settings.settings.gain != GAIN_HIGH) {
        uint8_t wired = app.player.volume;
        player_set_volume(&app.player, PLAYER_OUTPUT_WIRED,
                          wired > HEADPHONE_AUDIO_HIGH_GAIN_RISE_DB
                              ? (uint8_t)(wired - HEADPHONE_AUDIO_HIGH_GAIN_RISE_DB)
                              : 0);
        (void)player_save(&app.player);
        capture_settings();
    }
    app.settings.settings.dac_volume = settings.volume;
    app.settings.settings.gain = (GainLevel)settings.gain;
    app.settings.settings.output = (OutputJack)settings.output;
    settings_store_save(&app.settings);
    if (settings.headphones != app.headphones.profiles.current) {
        app.headphones_chosen = (uint8_t)(settings.headphones + 1);
    }
}

static void commit_dac_headphones(void)
{
    const SettingsModuleEntry *entry = app.headphones_chosen != 0 ? settings_module() : NULL;
    if (entry != NULL) {
        entry->dac_headphones(&app.headphones, (uint8_t)(app.headphones_chosen - 1), &app.player,
                              &app.settings);
    }
    app.headphones_chosen = 0;
}

void app_request_rescan(void)
{
    app.rescan_manual = true;
}

void app_note_internal_music_written(void)
{
    music_check_bump_generation();
}

void app_request_rescan_if_music_changed(void)
{
    default_theme_invalidate(&app.theme);
    app.music_check_requested = true;
}

typedef struct ChargeFrame {
    const ViewsModuleEntry *entry;
} ChargeFrame;

static void draw_charge(void *context, Canvas *canvas)
{
    const ChargeFrame *frame = context;
    const ViewsModuleEntry *entry = frame->entry;
    if (entry != NULL) {
        entry->charge_draw(canvas, board_theme(), app.charge_battery, app.now_ms);
    } else {
        usb_screen_draw_fallback(
            canvas, board_theme(), app.charge_battery,
            (AppUsbStatus){.view = APP_USB_POWER, .detail = i18n_text(STR_CHARGE_MODE_HINT)});
    }
}

static void open_usb_chooser(bool prompt)
{
    if (debug_runner_active() || hal_system_restart_pending() || app.saying_goodbye) {
        return;
    }
    Screen *screen = app_screens_usb_modes(&app.screens, prompt);
    uint32_t now_ms = hal_clock_milliseconds();
    if (screen == NULL) {
        explain_missing_screen(now_ms);
        return;
    }
    app.usb_chooser_charging |= app.charging;
    app.charging = false;
    app.usb_chooser = screen;
    power_policy_wake(&app.power, now_ms);
    set_display(true);
    if (top_screen() == screen) {
        screen->enter(screen->context, now_ms);
    } else if (apply_action(screen_action_push(screen), now_ms)) {
        screen_changed();
    }
}

CORE_SEAM void app_open_usb_modes(void)
{
    open_usb_chooser(false);
}

CORE_SEAM void app_ask_usb_mode(void)
{
    open_usb_chooser(true);
}

static void resume_charging(uint32_t now_ms, bool lit)
{
    app.usb_chooser_charging = false;
    app.charging = true;
    app.charge_battery = hal_power_battery();
    if (lit) {
        light_charge_screen(now_ms);
    } else {
        set_display(false);
    }
}

static void follow_usb_chooser(uint32_t now_ms)
{
    if (app.usb_chooser == NULL) {
        return;
    }
    const PlugChooser *usb = &app.screens.modules.settings.settings.usb;
    bool open = top_screen() == app.usb_chooser;
    bool quiet = (usb->timed || app.usb_chooser_charging) && (usb->expired || !app.display_on);
    bool lapsed = open && (quiet || !hal_power_cable());
    if (lapsed && apply_action(screen_action_pop(), now_ms)) {
        screen_changed();
        open = false;
    }
    if (open) {
        app.render_wanted |= usb->redraw;
        app.screens.modules.settings.settings.usb.redraw = false;
        return;
    }
    app.usb_chooser = NULL;
    if (app.usb_chooser_charging) {
        resume_charging(now_ms, !lapsed);
    }
}

static void follow_charge_keys(uint32_t now_ms)
{
    KeyEvent event;
    while (app.charging && key_tracker_next_event(&app.keys, &event)) {
        if (event.key == KEY_POWER && (event.gesture == KEY_GESTURE_LONG_PRESS ||
                                       ((app.settings.settings.power_on_flags & 0x10u) != 0 &&
                                        event.gesture == KEY_GESTURE_PRESS))) {
            stop_charging(now_ms);
        } else if (event.key == KEY_MENU && event.gesture == KEY_GESTURE_PRESS) {
            app_open_usb_modes();
        } else {
            light_charge_screen(now_ms);
        }
    }
}

static void charge_step(uint32_t now_ms)
{
    follow_charge_keys(now_ms);
    if (!app.charging) {
        return;
    }
    if (deadline_reached(now_ms, app.battery_check_ms)) {
        app.battery_check_ms = now_ms + BATTERY_CHECK_MS;
        HalBattery battery = hal_power_battery();
        app.render_wanted |= app_usb_battery_changed(app.charge_battery, battery);
        app.charge_battery = battery;
        const ViewsModuleEntry *entry =
            (const ViewsModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_VIEWS);
        if (entry != NULL) {
            entry->charge_observe(battery, now_ms);
        }
        if (!app.charge_battery.usb_power) {
            app.charging = false;
            debug_loop_shutdown(DEBUG_REPORT_UNPLUGGED);
            power_off_now();
            return;
        }
    }
    if (app.display_on && deadline_reached(now_ms, app.charge_lit_until_ms)) {
        set_display(false);
    }
    bool moving = app.charge_battery.usb_power && app.charge_battery.charging;
    bool due = deadline_reached(now_ms, app.pacer.last_ms + CHARGE_SCREEN_FRAME_MS);
    if (app.display_on && (app.render_wanted || (moving && due))) {
        ChargeFrame frame = {.entry = (const ViewsModuleEntry *)screen_modules_use(
                                 &app.screens, SCREEN_MODULE_VIEWS)};
        app.render_wanted = false;
        app.pacer.last_ms = now_ms;
        render((FrameDrawer){.draw = draw_charge, .context = &frame}, true);
        light_after_first_frame();
    } else {
        use_wanted_clock();
    }
}

static bool prepare_step(void)
{
    bool finished = false;
    bool pause =
        card_files_prepare_step(&app.prepare_index, &app.prepare_report, app.charging, &finished);
    if (finished) {
        app.prepare_pending = false;
        report_card_files(app.prepare_report, hal_clock_milliseconds());
    }
    return pause;
}

bool app_preparing(void)
{
    return app.prepare_pending;
}

static void frame_debug_screenshot(DebugScreenshot *shot)
{
    top_owned(app.now_ms);
    shot->renderer = frame_renderer(whole_screen(), false);
    shot->drawer = (FrameDrawer){.draw = draw_frame, .context = &app};
    app.status_bar = current_status_bar();
}

static const DebugAppHooks DEBUG_APP_HOOKS = {.power = &app.power,
                                              .player = &app.player,
                                              .screens = &app.screens,
                                              .settings = &app.settings,
                                              .charging = &app.charging,
                                              .rescan_manual = &app.rescan_manual,
                                              .rescan_requested = &app.rescan_full,
                                              .render_urgent = &app.render_urgent,
                                              .stop_charging = stop_charging,
                                              .settle = settle_screens,
                                              .apply = apply_action,
                                              .open_start = open_start_screen,
                                              .changed = screen_changed,
                                              .frame = frame_debug_screenshot};

static void combo_screenshot(void)
{
#ifdef ECHO_DEBUG_LOOP
    DebugScreenshot shot = {.slot = 0};
    frame_debug_screenshot(&shot);
    debug_runner_screenshot(&shot);
#endif
}

static void run_debug_script(uint32_t now_ms)
{
    debug_runner_step(
        now_ms, (DebugRunnerView){fade_running(), app.display_on, app.keys.states[KEY_POWER].held},
        &DEBUG_APP_HOOKS);
}

static void follow_destination(Screen **pending, uint32_t now_ms)
{
    Screen *destination = *pending;
    if (destination != NULL) {
        *pending = NULL;
        if (apply_action(screen_action_push(destination), now_ms)) {
            screen_changed();
            app.render_urgent = true;
        }
    }
}

static void service_discovery(uint32_t now_ms)
{
    if (!discovery_is_top()) {
        return;
    }
    const LibraryModuleEntry *module =
        (const LibraryModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_LIBRARY);
    if (module == NULL || module->step == NULL) {
        return;
    }
    module->step(&app.screens, now_ms);
    DiscoveryScreen *screen = &app.screens.modules.library.discovery;
    if (screen->destination != NULL) {
        Screen *destination = screen->destination;
        screen->destination = NULL;
        if (apply_action(screen_action_push(destination), now_ms)) {
            screen_changed();
            app.render_urgent = true;
        }
    }
}

static uint32_t view_stamp(uint32_t *position)
{
    PlayerStatus status = player_status(&app.player);
    HalBattery battery = hal_power_battery();
    WallNow wall = wall_time_now();
    const NowPlayingScreen *now_playing = &app.screens.now_playing;
    uint32_t since = app.now_ms - now_playing->feedback_started_ms;
    const uint32_t values[] = {
        (uint32_t)status.state,
        status.has_track,
        status.track,
        status.duration_ms,
        status.volume,
        (uint32_t)status.shuffle,
        (uint32_t)status.repeat,
        status.ab_repeat,
        status.place.number,
        status.place.total,
        app.settings.settings.status_items | (uint32_t)app.settings.settings.battery_percent << 8,
        battery.known,
        battery.percent,
        battery.charging,
        battery.charge_disabled,
        battery.usb_power,
        (uint32_t)bluetooth_of(&app.bluetooth),
        app.lock.locked,
        app.toast.visible,
        app.toast.until_ms,
        app.themes.active,
        (uint32_t)now_playing->feedback << 3 | (uint32_t)(since < NOW_PLAYING_BADGE_MS) << 2 |
            (uint32_t)(since < NOW_PLAYING_VOLUME_FEEDBACK_MS) << 1 |
            (!deadline_elapsed(app.now_ms, now_playing->repeat_feedback_ms, NOW_PLAYING_BADGE_MS)),
        wall.known ? wall.local_s / VIEW_STAMP_MINUTE_S : 0,
    };
    *position = status.position_ms / VIEW_STAMP_POSITION_MS;
    return hash_update(HASH_START, values, sizeof values);
}

typedef enum ViewChange {
    VIEW_STILL,
    VIEW_TIME,
    VIEW_FULL
} ViewChange;

__attribute__((noinline)) static ViewChange view_changed(uint32_t now_ms)
{
    if (app.view_stamp_known &&
        !deadline_elapsed(now_ms, app.view_checked_ms, VIEW_STAMP_CHECK_MS)) {
        return VIEW_STILL;
    }
    uint32_t position;
    uint32_t stamp = view_stamp(&position);
    ViewChange change = VIEW_STILL;
    if (app.view_stamp_known) {
        if (stamp != app.view_stamp) {
            change = VIEW_FULL;
        } else if (position != app.view_position && app.bands[MARQUEE_BAND_TIME].height > 0) {
            change = VIEW_TIME;
        }
    }
    app.view_position = position;
    app.view_stamp_known = true;
    app.view_stamp = stamp;
    app.view_checked_ms = now_ms;
    return change;
}

CORE_SEAM void app_step(void)
{
    if (app.powered_off) {
        return;
    }
    if (app.startup_pending) {
        app.startup_pending = false;
        if (!app.charging) {
            bluetooth_link_resume(&app.bluetooth, &app.toast, hal_clock_milliseconds());
        }
    }
    if (!app.power.standby_active) {
        if (app.prepare_pending && prepare_step()) {
            return;
        }
        if (!app.listening_loaded && !app.prepare_pending) {
            listening_store_load(&app.listening, &app.library.library);
            app.listening_loaded = true;
        }
        if (app.music_check_requested || app.rescan_manual) {
            app.music_check_requested = false;
            bool changed = hal_system_restart_pending() ||
                           (app.rescan_manual && walk_music(MUSIC_WALK_MANUAL));
            if (!changed) {
                show_working_notice(STR_MUSIC, i18n_text(STR_CHECKING_MUSIC), WIRE_SHAPE_DISC);
                changed = walk_music(MUSIC_WALK_CHECK) || !library_store_open(&app.library);
            }
            if (changed) {
                app_show_notice(i18n_text(STR_MUSIC), i18n_text(STR_CHECK_CHANGED));
                app.rescan_requested = true;
                app.rescan_stops = 0;
            } else if (app.rescan_manual) {
                log_scan(DEBUG_SCAN_UP_TO_DATE, 0);
                toast_show(
                    &app.toast,
                    i18n_text(app.music_unchecked ? STR_SCREEN_MISSING : STR_LIBRARY_UP_TO_DATE),
                    hal_clock_milliseconds());
            }
            app.rescan_manual = false;
        }
        if (app.rescan_flags != 0) {
            run_rescan();
            return;
        }
        if (app.language_requested) {
            app.language_requested = false;
            change_language(hal_clock_milliseconds());
        }
    }
    uint32_t started_us = hal_clock_microseconds();
    uint32_t now_ms = hal_clock_milliseconds();
    app.now_ms = now_ms;
    hal_audio_service();
    player_catch_up(&app.player);
    hal_bluetooth_service();
    run_debug_script(now_ms);
    if (!app.charging && !app.power.standby_active) {
        continue_mixtape(now_ms);
        settle_screens(now_ms);
    }
    read_input();
    read_headset();
    bool fast_list = app.display_on && top_screen()->fast_list;
    app.keys.config.timing.repeat_delay_ms =
        fast_list ? LIST_REPEAT_DELAY_MS : KEY_CONFIG.timing.repeat_delay_ms;
    app.keys.config.timing.repeat_interval_ms =
        fast_list ? LIST_REPEAT_INTERVAL_MS : KEY_CONFIG.timing.repeat_interval_ms;
    key_tracker_update(&app.keys, now_ms);
    if (!app.power.standby_active && key_lock_update(&app.lock, now_ms)) {
        toast_show(&app.toast,
                   i18n_text(app.lock.locked ? STR_BUTTONS_LOCKED : STR_BUTTONS_UNLOCKED), now_ms);
        if (!app.display_on) {
            power_policy_wake(&app.power, now_ms);
            set_display(true);
        }
    }
    if (app.charging) {
        charge_step(now_ms);
        return;
    }
    if (fade_running()) {
        drain_key_events();
    } else {
        debug_loop_boot(DEBUG_BOOT_INTERACTIVE);
        dispatch_key_events(now_ms);
    }
    if (app.power.standby_active) {
        update_power(now_ms);
        return;
    }
    if (app.reload_pending) {
        Toast toast = app.toast;
        app.reload_pending = false;
        app.rescan_flags = 0;
        app.music_check_requested = false;
        bool restored = hal_system_settle_user_state(true);
        start_app(false);
        toast_show(&app.toast, restored ? toast.text : i18n_text(STR_RESTORE_FAILED),
                   hal_clock_milliseconds());
        return;
    }
    if (app.saying_goodbye && fade_finished(&app.goodbye, now_ms)) {
        finish_shut_down();
    }
    if (!app.powered_off) {
        player_update(&app.player, now_ms);
        if (app.listening_loaded) {
            listening_store_follow(&app.listening, listening_observation(), now_ms);
        }
        follow_listeners();
        bluetooth_link_step(&app.bluetooth, &app.player, now_ms);
        spoken_store_follow(&app.spoken, player_playback_status(&app.player), now_ms);
        hal_audio_set_effects(listening_effects_choose(
            &app.settings.settings, &app.headphones.profiles, app.player.spoken));
        explain_refusals(now_ms);
        debug_loop_follow(now_ms, &app.player);
        update_power(now_ms);
    }
    follow_scrub(now_ms);
    if (app.powered_off || app.charging) {
        return;
    }
    toast_update(&app.toast, now_ms);
    explain_lowered_volume(now_ms);
    top_owned(now_ms);
    follow_appearance();
    update_screen(now_ms);
    follow_usb_chooser(now_ms);
    commit_dac_headphones();
    service_discovery(now_ms);
    continue_covers(now_ms);
    MixtapeUi *mixtapes = &app.screens.modules.views.mixtapes;
    if ((app.screens.bound_modules & (1u << SCREEN_MODULE_VIEWS)) != 0 &&
        mixtapes->destination != NULL) {
        follow_destination(&mixtapes->destination, now_ms);
    }
    MomentsScreen *moments = &app.screens.modules.listening.moments;
    if (top_screen() == &moments->screen && moments->destination != NULL) {
        follow_destination(&moments->destination, now_ms);
    }
    if (top_screen() == &app.screens.now_playing.screen) {
        follow_destination(&app.screens.now_playing.destination, now_ms);
    }
    follow_destination(&app.screens.shortcut_destination, now_ms);
    compose_toast();
    render_when_due(now_ms, started_us);
    wire_motion_end(&app.motion, &app.notice);
    FrameDrawer drawer = {.draw = draw_frame, .context = &app};
    top_owned(now_ms);
    animate(drawer, top_screen(), app.display_on && !fade_running() && !slide_running(), now_ms);
}

static bool marquee_step_reached(uint32_t now_ms)
{
    if (!app.marquee_due || !deadline_reached(now_ms, app.marquee_due_ms)) {
        return false;
    }
    app.marquee_due = false;
    return true;
}

static void follow_marquees(uint32_t now_ms)
{
    uint32_t wait_ms = marquee_watch_wait_ms();
    app.marquee_due = wait_ms != MARQUEE_STILL;
    app.marquee_due_ms = now_ms + wait_ms;
    for (int band = 0; band < MARQUEE_BAND_COUNT; band++) {
        app.bands[band] = marquee_watch_band((MarqueeBand)band);
    }
}

static uint8_t bands_for(bool stepped, bool timed)
{
    if (stepped && app.bands[MARQUEE_BAND_TEXT].height <= 0) {
        return 0;
    }
    return (uint8_t)(stepped << MARQUEE_BAND_TEXT | timed << MARQUEE_BAND_TIME |
                     timed << MARQUEE_BAND_TAPE);
}

static bool playback_view_rests(void)
{
    const Screen *screen = top_screen();
    const AppScreens *screens = &app.screens;
    const SettingsScreens *settings = &screens->modules.settings.settings;
    return (uintptr_t)screen - (uintptr_t)&screens->browse < sizeof screens->browse ||
           (screen == &screens->home.screen && !screens->home_art.cycling) ||
           screen == &screens->music.screen || screen == &settings->menu.screen ||
           screen == &settings->group_menu.screen ||
           (screen == &screens->modules.views.mixtapes.screen && !mixtape_busy());
}

static void render_when_due(uint32_t now_ms, uint32_t started_us)
{
    if (!app.display_on) {
        return;
    }
    if (app.screens.now_playing.scrub.active) {
        app.pacer.fast_frames = 0;
    }
    ViewChange change = view_changed(now_ms);
    bool stepped = marquee_step_reached(now_ms);
    bool urgent = app.render_urgent || app.light_after_frame || fade_running() || slide_running();
    bool quiet = !app.render_wanted && !urgent && app.pacer.fast_frames == 0;
    uint8_t mask = quiet && change != VIEW_FULL ? bands_for(stepped, change == VIEW_TIME) : 0;
    app.render_wanted |= change != VIEW_STILL || stepped;
    if (!frame_pacer_due(&app.pacer, now_ms, urgent || app.render_wanted) ||
        (quiet && !app.render_wanted && app.pacer.quiet_frames >= FRAME_PACER_IDLE_FRAMES &&
         app.player.state == PLAYER_PLAYING && playback_view_rests())) {
        return;
    }
    app.render_urgent = false;
    app.render_wanted = false;
    frame_stats_record(&app.update_stats, hal_clock_microseconds() - started_us);
    app.frame_sent = false;
    app.band_mask = top_owned(now_ms) ? mask : 0;
    marquee_watch_start();
    render_frame();
    app.band_mask = 0;
    NowPlayingScreen *now_playing = &app.screens.now_playing;
    if (now_playing->scrub.active && top_screen() == &now_playing->screen) {
        now_playing->scrub.shown_ms = now_playing->scrub.target_ms;
    }
    follow_marquees(now_ms);
    frame_pacer_done(&app.pacer, now_ms, app.frame_sent, urgent);
    light_after_first_frame();
    frame_stats_record(&app.frame_stats, hal_clock_microseconds() - started_us);
    debug_runner_frame(
        (DebugFrameSources){&app.frame_stats, &app.update_stats, &app.draw_stats, &app.send_stats});
}

bool app_theme_pack_active(void)
{
    return app.themes.active;
}

bool app_charging(void)
{
    return app.charging;
}

uint32_t app_pass_interval_ms(void)
{
    if (app.power.standby_active) {
        return 20;
    }
    bool visual = app.display_on && (app.pacer.fast_frames > 0 || app.render_urgent ||
                                     app.render_wanted || app.charging);
    bool busy = visual || fade_running() || slide_running() || app.prepare_pending ||
                app.rescan_flags != 0 || app.music_check_requested || app.rescan_manual;
    if (busy) {
        return PASS_BUSY_MS;
    }
    return app.display_on ? PASS_SCREEN_ON_MS : PASS_SCREEN_OFF_MS;
}

typedef struct UsbStatusFrame {
    AppUsbStatus status;
    HalBattery battery;
} UsbStatusFrame;

static void draw_usb_status(void *context, Canvas *canvas)
{
    const UsbStatusFrame *frame = context;
    AppUsbRenderer draw = app.usb_renderer;
    if (draw == NULL) {
        draw = usb_screen_draw_fallback;
    }
    draw(canvas, board_theme(), frame->battery, frame->status);
}

CORE_SEAM void app_set_usb_renderer(AppUsbRenderer renderer)
{
    app.usb_renderer = renderer;
}

CORE_SEAM void app_show_usb_status(AppUsbStatus status)
{
    UsbStatusFrame frame = {.status = status, .battery = hal_power_battery()};
    if (!app.display_on) {
        set_display(true);
    }
    bool themed = app.themes.active;
    render((FrameDrawer){.draw = draw_usb_status, .context = &frame}, false);
    if (themed && !app.themes.active) {
        render((FrameDrawer){.draw = draw_usb_status, .context = &frame}, false);
    }
    light_after_first_frame();
}

CORE_SEAM void app_show_usb_area(AppUsbStatus status, Rect area)
{
    UsbStatusFrame frame = {.status = status, .battery = hal_power_battery()};
    render_area(area, (FrameDrawer){.draw = draw_usb_status, .context = &frame});
    frame_diff_mask(&app.frame_diff, area);
    frame_diff_mask(&app.frame_diff, (Rect){0, 0, 0, 0});
}

CORE_SEAM void app_show_notice(const char *title, const char *message)
{
    bool working = app.motion.running && app.motion.owner == &app.notice;
    if (working) {
        wire_motion_tick(&app.motion, hal_clock_milliseconds());
    }
    StringBuilder copy = string_builder_make(app.notice_message, sizeof app.notice_message);
    string_builder_append(&copy, message != NULL ? message : "");
    MessageConfig config = {.title = title,
                            .message = app.notice_message,
                            .theme = &notice_theme,
                            .motion = working ? &app.motion : NULL};
    Screen *screen = message_screen_init(&app.notice, config);
    if (!app.display_on) {
        set_display(true);
    }
    render((FrameDrawer){.draw = draw_notice, .context = screen}, false);
    light_after_first_frame();
}

CORE_SEAM void app_show_splash(void)
{
    app.now_ms = hal_clock_milliseconds();
    start_fade(&app.splash, SPLASH_TIMINGS, app.light_after_frame && !app.charging);
}

void app_draw_start(void)
{
    if (app.charging) {
        charge_step(hal_clock_milliseconds());
    } else {
        render_when_due(hal_clock_milliseconds(), hal_clock_microseconds());
    }
}

void app_wake(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    power_policy_wake(&app.power, now_ms);
    if (app.charging) {
        light_charge_screen(now_ms);
    }
}

void app_resume_after_usb(void)
{
    bool locked = app.lock.locked;
    key_tracker_release_all(&app.keys);
    app.lock = (KeyLock){.locked = locked};
    app.raw_held = 0;
    app.loader_keys = false;
    app.chord = (KeyChord){.allowed = false};
    key_crossing_reset(&app.crossing);
    app_wake();
}

const FrameStats *app_frame_stats(void)
{
    return &app.frame_stats;
}

DebugFrameSources app_debug_frame_sources(void)
{
    return (DebugFrameSources){&app.frame_stats, &app.update_stats, &app.draw_stats,
                               &app.send_stats};
}

const char *app_screen_title(void)
{
    return top_screen()->title;
}

uint16_t app_status_items(void)
{
    StatusBarState bar = shown_status_bar(&app, top_screen());
    return status_bar_items(board_theme(), &bar);
}

Rect app_status_item_area(uint16_t item)
{
    StatusBarState bar = shown_status_bar(&app, top_screen());
    return status_bar_item_area(board_theme(), &bar, item);
}

Rect app_header_title_area(void)
{
    return header_title_bounds(board_theme());
}

int app_screen_depth(void)
{
    return screen_stack_depth(&app.stack);
}

void app_stop(void)
{
    if (!app.powered_off) {
        save_session();
    }
}

PlayerStatus app_player_status(void)
{
    return player_status(&app.player);
}

const char *app_toast_text(void)
{
    return app.toast.visible ? app.toast.text : NULL;
}

const char *app_diagnostics_value(int index)
{
    if (screen_modules_use(&app.screens, APP_ABOUT_MODULE) == NULL) {
        return NULL;
    }
    const MenuItem *item = &app_screens_about(&app.screens)->diagnostics_items[index];
    return item->value != NULL ? item->value(item->context) : NULL;
}

const Standby *app_standby(void)
{
    return &app.standby;
}
