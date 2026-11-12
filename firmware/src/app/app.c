#include "app/app.h"
#include "core/seam.h"

#include <stddef.h>

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
#include "favorites_store.h"
#include "frame_overlay.h"
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
#include "modules/modules.h"
#include "music_check.h"
#include "player.h"
#include "scan_guard.h"
#include "screens/charge_screen.h"
#include "settings/rocker.h"
#include "settings_store.h"
#include "ui/fade.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "ui/screen_stack.h"
#include "ui/slide.h"
#include "ui/status_bar.h"
#include "ui/toast.h"
#include "ui/wire_motion.h"
#include "wall_time.h"

enum {
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
            .click_window_ms = 300,
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
            [KEY_VOLUME_UP] = 1000,
            [KEY_VOLUME_DOWN] = 1000,
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
    Orientation shown_orientation;
    bool rocker_swapped[KEY_COUNT];
    FavoritesStore favorites;
    SpokenStore spoken;
    CoverCache covers;
    PowerPolicy power;
    LowBattery low_battery;
    uint32_t battery_check_ms;
    bool usb_power;
    bool rescan_requested;
    uint8_t rescan_stops;
    bool covers_pending;
    bool rescan_manual;
    uint16_t covers_album;
    bool language_requested;
    bool music_check_requested;
    bool storage_formatted;
    bool format_success;
    bool prepare_pending;
    size_t prepare_index;
    CardFilesReport prepare_report;
    bool light_after_frame;
    StringId goodbye_text;
    Toast toast;
    BluetoothLink bluetooth;
    uint32_t shown_refusals;
    StatusBarState status_bar;
    char battery_text[STATUS_BAR_PERCENT_CAPACITY];
    char clock_text[STATUS_BAR_CLOCK_CAPACITY];
    char volume_text[4];
    char position_text[24];
    AppUsbRenderer usb_renderer;
    AppScreens screens;
    MessageScreen notice;
    WireMotion motion;
    FrameDiff frame_diff;
    FramePacer pacer;
    bool render_urgent;
    bool render_wanted;
    bool view_stamp_known;
    uint32_t view_stamp;
    uint32_t view_checked_ms;
    bool frame_sent;
    Fade splash;
    Fade goodbye;
    Slide slide;
    Screen *leaving;
    bool saying_goodbye;
    uint32_t now_ms;
    AppOptions options;
    bool display_on;
    bool powered_off;
    bool charging;
    bool usb_chooser_charging;
    Screen *usb_chooser;
    uint32_t charge_lit_until_ms;
    bool volume_changed;
    KeyLock lock;
    uint32_t volume_changed_ms;
    HalBattery charge_battery;
    HalAudioJack listener_jack;
    bool listener_bluetooth;
    bool volume_lowered;
    uint8_t lowered_volume;
} AppState;

static AppState app;

static const FadeTiming SPLASH_TIMING = {.fade_ms = 600, .hold_ms = 500, .rising = true};
static const uint32_t SCREEN_ON_CLOCK_MHZ = 200;
static const uint32_t IDLE_CLOCK_MHZ = 96;
static const FadeTiming GOODBYE_TIMING = {.fade_ms = 600, .hold_ms = 400, .rising = false};
static Color strip_pixels[HAL_DISPLAY_WIDTH * BOARD_STRIP_HEIGHT];
static uint32_t row_hashes[HAL_DISPLAY_LONG_SIDE];
static Theme notice_theme;
static Theme compact_theme;

static void follow_compact_theme(void);
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
    follow_compact_theme();
}

static Key oriented_key(HalKeyTransition transition)
{
    if (transition.pressed) {
        Orientation orientation = app.settings.settings.orientation;
        bool changes_volume = top_screen()->hold_volume_keys;
        app.rocker_swapped[transition.key] =
            app.display_on && (changes_volume ? rocker_volume_swapped_in(orientation)
                                              : rocker_swapped_in(orientation));
    }
    return rocker_key(transition.key, app.rocker_swapped[transition.key]);
}

static void take_transition(HalKeyTransition transition)
{
    hal_system_coast(false);
    app.render_urgent = true;
    Key key = oriented_key(transition);
    switch (key_lock_transition(&app.lock, key, transition.pressed, transition.time_ms)) {
    case KEY_LOCK_PASS:
        if (transition.pressed) {
            key_tracker_key_down(&app.keys, key, transition.time_ms);
        } else {
            key_tracker_key_up(&app.keys, key, transition.time_ms);
        }
        break;
    case KEY_LOCK_COMBO_STARTED:
        key_tracker_cancel(&app.keys, KEY_PLAY);
        key_tracker_cancel(&app.keys, KEY_POWER);
        break;
    case KEY_LOCK_REFUSED:
        toast_show(&app.toast, i18n_text(STR_LOCKED_HINT), transition.time_ms);
        break;
    case KEY_LOCK_SWALLOW:
        break;
    }
}

static void read_input(void)
{
    HalKeyTransition transition;
    while (hal_input_next_transition(&transition)) {
        if (transition.pressed) {
            debug_runner_key_pressed();
        }
        take_transition(transition);
    }
    while (debug_runner_next_key(&transition)) {
        take_transition(transition);
    }
}

static void use_screen_key_behaviors(void);

static uint32_t wanted_clock_mhz(void)
{
    if (app.display_on || !hal_audio_status().idle) {
        return SCREEN_ON_CLOCK_MHZ;
    }
    return IDLE_CLOCK_MHZ;
}

static void use_wanted_clock(void)
{
    uint32_t wanted = wanted_clock_mhz();
    if (hal_system_clock_mhz() != wanted) {
        hal_system_set_clock_mhz(wanted);
    }
}

static void light_after_first_frame(void)
{
    if (app.light_after_frame) {
        app.light_after_frame = false;
        hal_display_set_power(true);
    }
}

static void set_display(bool on)
{
    app.display_on = on;
    app.light_after_frame = on;
    if (!on) {
        hal_display_set_power(false);
    }
    use_wanted_clock();
    if (screen_stack_depth(&app.stack) > 0) {
        use_screen_key_behaviors();
    }
}

static void save_settings(void)
{
    settings_store_capture(&app.settings, player_status(&app.player));
    settings_store_save(&app.settings);
}

static ListeningObservation listening_observation(void)
{
    ListeningOutput output =
        app.bluetooth.connected && !app.bluetooth.wired ? LISTENING_OUTPUT_BLUETOOTH
        : hal_audio_jack() == HAL_AUDIO_JACK_BALANCED   ? LISTENING_OUTPUT_BALANCED
                                                        : LISTENING_OUTPUT_SINGLE_ENDED;
    return (ListeningObservation){player_status(&app.player), app.player.current_token, output};
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

static void save_session(void)
{
    cancel_edits();
    flush_listening();
    spoken_store_flush(&app.spoken, player_status(&app.player), hal_clock_milliseconds());
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

static void lower_start_volume(void)
{
    uint8_t volume = player_status(&app.player).volume;
    uint8_t safe = settings_safe_start_volume(&app.settings.settings, volume);
    if (safe < volume) {
        player_change_volume(&app.player, (int)safe - (int)volume);
        app.volume_lowered = true;
        app.lowered_volume = safe;
    }
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
    if (plugged || linked) {
        lower_start_volume();
    }
}

static void explain_lowered_volume(uint32_t now_ms)
{
    static char text[TOAST_TEXT_CAPACITY];
    if (!app.volume_lowered || app.toast.visible || !app.display_on || fade_running()) {
        return;
    }
    app.volume_lowered = false;
    if (player_status(&app.player).volume == app.lowered_volume) {
        StringBuilder builder = string_builder_make(text, sizeof text);
        i18n_format_counts(&builder, STR_VOLUME_LOWERED, app.lowered_volume, 0);
        toast_show(&app.toast, text, now_ms);
    }
}

static void stop_charging(uint32_t now_ms)
{
    app.charging = false;
    lower_start_volume();
    bluetooth_link_start(&app.bluetooth, &app.toast, now_ms);
    power_policy_wake(&app.power, now_ms);
    if (!app.display_on) {
        set_display(true);
    }
    app_show_splash();
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

static void shut_down(StringId text)
{
    app.goodbye_text = text;
    save_session();
    if (!app.display_on) {
        finish_shut_down();
        return;
    }
    fade_start(&app.goodbye, app.now_ms, GOODBYE_TIMING);
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

static void set_key_behaviors(KeyBehavior volume, KeyBehavior play)
{
    key_tracker_set_behavior(&app.keys, KEY_VOLUME_UP, volume);
    key_tracker_set_behavior(&app.keys, KEY_VOLUME_DOWN, volume);
    key_tracker_set_behavior(&app.keys, KEY_PLAY, play);
}

static void use_screen_key_behaviors(void)
{
    bool skipping = !app.display_on || top_screen()->hold_volume_keys;
    if (!skipping) {
        set_key_behaviors(KEY_BEHAVIOR_REPEAT, KEY_BEHAVIOR_TAP_OR_HOLD);
        return;
    }
    switch (app.settings.settings.skip_gesture) {
    case SKIP_GESTURE_HOLD:
        set_key_behaviors(KEY_BEHAVIOR_TAP_HOLD_REPEAT, KEY_BEHAVIOR_TAP_OR_HOLD);
        break;
    case SKIP_GESTURE_DOUBLE_VOLUME:
        set_key_behaviors(KEY_BEHAVIOR_DOUBLE_CLICK_REPEAT, KEY_BEHAVIOR_TAP_OR_HOLD);
        break;
    case SKIP_GESTURE_PLAY_CLICKS:
        set_key_behaviors(KEY_BEHAVIOR_REPEAT, KEY_BEHAVIOR_TRIPLE_CLICK_OR_HOLD);
        break;
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
    if (settings->button_actions[PLAYBACK_PLAY_TAP] != BUTTON_ACTION_DEFAULT ||
        settings->button_actions[PLAYBACK_PLAY_HOLD] != BUTTON_ACTION_DEFAULT) {
        key_tracker_set_behavior(&app.keys, KEY_PLAY, KEY_BEHAVIOR_TAP_OR_HOLD);
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
           top_screen() != &app.screens.now_playing.screen && !top_screen()->takes_every_key;
}

static ScreenAction now_playing_action(void)
{
    return top_screen() == &app.screens.modules.views.up_next.screen
               ? screen_action_pop()
               : screen_action_push(&app.screens.now_playing.screen);
}

static bool screen_ready(const Screen *screen)
{
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
    if (top_screen() == &app.screens.modules.settings.settings.choice.screen &&
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
    PlayerStatus status = player_status(&app.player);
    Screen *top = top_screen();
    return status.has_track && status.state == PLAYER_PLAYING &&
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
    if (command != NOW_PLAYING_NONE) {
        app.volume_changed = false;
    }
    switch (command) {
    case NOW_PLAYING_TOGGLE_PAUSE:
        player_toggle_pause(&app.player);
        break;
    case NOW_PLAYING_NEXT:
        player_skip(&app.player, 1);
        break;
    case NOW_PLAYING_PREVIOUS:
        player_skip(&app.player, -1);
        break;
    case NOW_PLAYING_VOLUME_UP:
    case NOW_PLAYING_VOLUME_DOWN:
        player_change_volume(&app.player, command == NOW_PLAYING_VOLUME_UP ? 1 : -1);
        app.volume_changed = true;
        app.volume_changed_ms = app.now_ms;
        break;
    case NOW_PLAYING_SEEK_FORWARD:
        now_playing_seek_by(&app.player, NOW_PLAYING_SCRUB_STEP_MS);
        break;
    case NOW_PLAYING_SEEK_FORWARD_FAST:
        now_playing_seek_by(&app.player, NOW_PLAYING_SCRUB_FAST_STEP_MS);
        break;
    case NOW_PLAYING_SEEK_BACK:
        now_playing_seek_by(&app.player, -NOW_PLAYING_SCRUB_STEP_MS);
        break;
    case NOW_PLAYING_SEEK_BACK_FAST:
        now_playing_seek_by(&app.player, -NOW_PLAYING_SCRUB_FAST_STEP_MS);
        break;
    default:
        break;
    }
}

static void run_background(KeyEvent event)
{
    bool adjusting =
        app.volume_changed && now_playing_adjusting_volume(app.volume_changed_ms, app.now_ms);
    run_player_command(now_playing_command_for(
        event, (NowPlayingKeys){.adjusting_volume = adjusting,
                                .skip = app.settings.settings.skip_gesture}));
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
        if (!app.powered_off && !app.charging && !app.usb_chooser_charging && !fade_running()) {
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

static void mapped_navigation(ButtonAction action, uint32_t now_ms)
{
    cancel_edits();
    app_wake();
    if (!app.display_on) {
        set_display(true);
    }
    if (action == BUTTON_ACTION_HOME) {
        go_home(now_ms);
    } else if (player_status(&app.player).has_track) {
        app_wake();
        Screen *screen = action == BUTTON_ACTION_UP_NEXT ? &app.screens.modules.views.up_next.screen
                                                         : &app.screens.now_playing.screen;
        if (top_screen() != screen && apply_action(screen_action_push(screen), now_ms)) {
            screen_changed();
        }
        if (action == BUTTON_ACTION_SEEK && top_screen() == screen) {
            dispatch_to_screen((KeyEvent){.key = KEY_PLAY, .gesture = KEY_GESTURE_LONG_PRESS},
                               now_ms);
        }
    }
}

static bool mapped_playback(KeyEvent event, uint32_t now_ms)
{
    PlaybackButton button;
    if (app.screens.now_playing.scrubbing) {
        return false;
    }
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
    switch (action) {
    case BUTTON_ACTION_PLAY_PAUSE:
        run_player_command(NOW_PLAYING_TOGGLE_PAUSE);
        break;
    case BUTTON_ACTION_NEXT:
        run_player_command(NOW_PLAYING_NEXT);
        break;
    case BUTTON_ACTION_PREVIOUS:
        run_player_command(NOW_PLAYING_PREVIOUS);
        break;
    case BUTTON_ACTION_VOLUME_UP:
        run_player_command(NOW_PLAYING_VOLUME_UP);
        break;
    case BUTTON_ACTION_VOLUME_DOWN:
        run_player_command(NOW_PLAYING_VOLUME_DOWN);
        break;
    case BUTTON_ACTION_UP_NEXT:
    case BUTTON_ACTION_SEEK:
    case BUTTON_ACTION_HOME:
        mapped_navigation(action, now_ms);
        break;
    default:
        break;
    }
    return true;
}

static void dispatch_key_events(uint32_t now_ms)
{
    KeyEvent event;
    while (!app.powered_off && key_tracker_next_event(&app.keys, &event)) {
        app.render_urgent = true;
        debug_loop_key(event);
        if (ignored_power_tap(event)) {
            continue;
        }
        PowerKeyOutcome outcome = power_policy_key(&app.power, event, now_ms);
        if (outcome.shutdown) {
            debug_loop_shutdown(DEBUG_REPORT_SHUTDOWN);
            shut_down(STR_GOODBYE);
        } else if (event.key == KEY_BACK && event.gesture == KEY_GESTURE_LONG_PRESS) {
            app_wake();
            set_display(true);
            go_home(now_ms);
        } else if (mapped_playback(event, now_ms)) {
            continue;
        } else if (outcome.background && app.screens.now_playing.scrubbing) {
            app_wake();
            set_display(true);
            dispatch_to_screen(event, now_ms);
        } else if (outcome.background) {
            run_background(event);
        } else if (!outcome.consumed) {
            dispatch_to_screen(event, now_ms);
        }
    }
}

static bool battery_empty(uint32_t now_ms)
{
    if (!deadline_reached(now_ms, app.battery_check_ms)) {
        return false;
    }
    app.battery_check_ms = now_ms + BATTERY_CHECK_MS;
    HalBattery battery = hal_power_battery();
    app.usb_power = battery.usb_power;
    LowBatteryReading reading = {
        .known = battery.known, .millivolts = battery.millivolts, .usb_power = battery.usb_power};
    return low_battery_update(&app.low_battery, reading, now_ms);
}

static void keep_awake_while_busy(uint32_t now_ms)
{
    if (app.usb_power || player_status(&app.player).state == PLAYER_PLAYING ||
        debug_runner_active() || player_queue_busy(&app.player) || mixtape_busy()) {
        power_policy_stay_awake(&app.power, now_ms);
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
    bool sleep_timer = app.power.sleep_timer_active;
    if (power_policy_update(&app.power, now_ms)) {
        bool slept = sleep_timer && !app.power.sleep_timer_active;
        debug_loop_shutdown(slept ? DEBUG_REPORT_SLEEP_TIMER : DEBUG_REPORT_AUTO_OFF);
        shut_down(STR_GOODBYE);
        return;
    }
    if (app.power.display_on == app.display_on) {
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

static void draw_fade(Canvas *canvas, const Fade *fade, const char *text)
{
    const Theme *theme = board_theme();
    Rect screen = whole_screen();
    Color color =
        color_blend(theme->colors.background, theme->colors.text, fade_alpha(fade, app.now_ms));
    TextStyle style = {.font = theme->fonts.title, .color = color};
    label_draw(canvas, style,
               (Label){.area = label_centered_area(style.font, screen, text), .text = text});
}

static bool draw_running_fade(Canvas *canvas)
{
    if (app.saying_goodbye) {
        draw_fade(canvas, &app.goodbye, i18n_text(app.goodbye_text));
        return true;
    }
    if (!fade_finished(&app.splash, app.now_ms)) {
        draw_fade(canvas, &app.splash, board_splash_text(board_theme()));
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

static void follow_compact_theme(void)
{
    compact_theme = *board_theme();
    if (theme_is_portrait(&compact_theme)) {
        compact_theme.metrics.header_height = compact_theme.metrics.status_height;
    }
}

static bool is_compact(const Screen *screen)
{
    return screen == &app.screens.now_playing.screen;
}

static void draw_slide(Canvas *canvas, Screen *arriving)
{
    Screen *leaving = app.leaving;
    const Theme *theme =
        is_compact(arriving) || is_compact(leaving) ? &compact_theme : board_theme();
    bool whole = arriving->full_screen || leaving->full_screen;
    Rect area = whole ? whole_screen() : theme_content_area(theme);
    int width = app.theme.metrics.screen_width;
    draw_shifted(leaving, canvas, slide_leaving_x(&app.slide, app.now_ms, width), area);
    draw_shifted(arriving, canvas, slide_arriving_x(&app.slide, app.now_ms, width), area);
    if (!whole) {
        bool arrived = slide_shows_arriving_title(&app.slide, app.now_ms);
        header_draw(canvas, theme, arrived ? arriving->title : leaving->title);
    }
}

static StatusBarState shown_status_bar(const AppState *state, const Screen *screen)
{
    StatusBarState bar = state->status_bar;
    const Theme *theme = board_theme();
    const char *title = screen->title;
    if (title != NULL && theme->metrics.status_height == 0) {
        bar.title_width = screen->header_title_width != 0 ? screen->header_title_width
                                                          : text_width(theme->fonts.header, title);
    }
    return bar;
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
    if (!screen->full_screen) {
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
    strip_renderer_render(&renderer, drawer, sink);
    hal_display_present();
}

static void send_motion(void *context, Rect area, const Color *pixels)
{
    (void)context;
    hal_audio_service();
    hal_bluetooth_service();
    hal_display_write(area.x, area.y, area.width, area.height, pixels);
}

static void render_motion(FrameDrawer drawer)
{
    StripRenderer renderer = frame_renderer(app.motion.area, false);
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

static void animate(FrameDrawer drawer, const void *owner, bool visible, uint32_t now_ms)
{
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
        render_motion(drawer);
    }
}

static void render_frame(void)
{
    uint32_t started_us = hal_clock_microseconds();
    app.send_us = 0;
    app.status_bar = current_status_bar();
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

static void start_playback(void)
{
    flush_listening();
    app.listening_loaded = false;
    library_store_open(&app.library);
    player_init(&app.player, &app.library.library);
    settings_store_restore_playback(&app.settings, &app.player, hal_clock_microseconds());
    player_restore(&app.player);
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
    default_theme_set_appearance(&app.theme, shown->appearance);
    default_theme_set_accent(&app.theme, shown->accent);
    notice_theme.colors = app.theme.colors;
    notice_theme.horizon = app.theme.horizon;
    notice_theme.header_rule = app.theme.header_rule;
    default_theme_set_text_size(&app.theme, shown->text_size);
}

static void start_hardware(uint32_t now_ms)
{
    power_policy_init(&app.power, settings_screen_timeout_ms(&app.settings.settings), now_ms);
    power_policy_set_auto_off(&app.power, settings_auto_off_ms(&app.settings.settings), now_ms);
    settings_store_apply_hardware(&app.settings.settings);
    set_display(true);
}

static void open_start_screen(uint32_t now_ms)
{
    Screen *path[START_PATH_CAPACITY];
    int count = app_screens_start_path(&app.screens, app.settings.settings.start_screen, path);
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
        .compact_theme = &compact_theme,
        .store = &app.library,
        .player = &app.player,
        .spoken = &app.spoken,
        .settings = &app.settings,
        .headphones = &app.headphones,
        .favorites = &app.favorites,
        .power = &app.power,
        .toast = &app.toast,
        .covers = &app.covers,
        .rescan_requested = &app.rescan_manual,
        .language_requested = &app.language_requested,
        .bluetooth = &app.bluetooth,
        .listening = &app.listening,
        .motion = &app.motion,
    };
    Screen *root = app_screens_init(&app.screens, context);
    screen_stack_init(&app.stack, root, now_ms);
}

static void start_screens(uint32_t now_ms)
{
    build_screens(now_ms);
    open_start_screen(now_ms);
    screen_changed();
}

static void change_language(uint32_t now_ms)
{
    Settings *settings = &app.settings.settings;
    language_load(settings->language);
    app.toast.visible = false;
    app.slide.active = false;
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

static void request_start_scan(bool library_created)
{
    app.covers_pending = music_check_covers_pending();
    uint32_t stopped = music_check_interrupted_scans();
    bool resume = stopped > 0 && stopped <= SCAN_GUARD_RETRIES;
    if (library_created || resume) {
        app.rescan_requested = true;
        app.rescan_stops = resume ? (uint8_t)stopped : 0;
    } else if (stopped == 0 && music_check_firmware_is_new()) {
        app.music_check_requested = true;
    }
}

static void report_card_files(CardFilesReport files, uint32_t now_ms)
{
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
    app.prepare_index = 0;
    app.prepare_report = (CardFilesReport){.library_created = false, .card_full = false};
    settings_store_load(&app.settings);
    language_load(app.settings.settings.language);
    headphone_store_load(&app.headphones);
    app.theme = BOARD_THEME;
    app.screens.bound_modules = 0;
    apply_orientation(app.settings.settings.orientation);
    follow_appearance();
    start_playback();
    start_hardware(now_ms);
    bluetooth_link_resume(&app.bluetooth, &app.toast, now_ms);
    start_screens(now_ms);
    note_listeners();
    app.volume_lowered = false;
    if (hal_power_take_usb_start()) {
        start_charging(now_ms);
    } else if (power_on) {
        lower_start_volume();
    }
}

int16_t app_clock_offset(void)
{
    return app.settings.settings.clock_offset_minutes;
}

CORE_COLD bool app_store_clock_offset(int16_t offset)
{
    settings_store_capture(&app.settings, player_status(&app.player));
    bool saved = settings_store_set_clock_offset(&app.settings, offset);
    if (!saved) {
        toast_show(&app.toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
    return saved;
}

void app_start(void)
{
    start_app(true);
}

static void show_rescan_progress(uint32_t files, uint32_t tracks)
{
    static uint32_t shown_ms;
    uint32_t now_ms = hal_clock_milliseconds();
    hal_bluetooth_service();
    if (files > 1 && now_ms - shown_ms < RESCAN_PROGRESS_MS) {
        animate_notice();
        return;
    }
    shown_ms = now_ms;
    char text[NOTICE_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_SCAN_TRACKS, tracks, 0);
    app_show_notice(i18n_text(STR_SCANNING), text);
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

static MusicFingerprint walk_music(void)
{
    const LibraryModuleEntry *module =
        (const LibraryModuleEntry *)screen_modules_use(&app.screens, SCREEN_MODULE_LIBRARY);
    return module != NULL ? module->music_fingerprint() : (MusicFingerprint){.complete = false};
}

static void continue_covers(uint32_t now_ms)
{
    bool idle = now_ms - app.power.last_activity_ms >= COVERS_IDLE_MS;
    bool rested = !app.player.loaded || (app.player.state == PLAYER_PAUSED &&
                                         now_ms - app.player.paused_since_ms >= COVERS_PAUSED_MS);
    if (!app.covers_pending || !idle || !rested || debug_runner_active() || hal_input_waiting()) {
        return;
    }
    if (app.player.loaded) {
        app_release_audio();
    }
    hal_system_set_clock_mhz(SCREEN_ON_CLOCK_MHZ);
    RescanOutcome outcome = library_rescan_covers(app.covers_album);
    use_wanted_clock();
    app.covers_album = (uint16_t)outcome.covers.next_album;
    app.covers_pending = outcome.cover_result == SCAN_STOPPED;
    cover_cache_open(&app.covers, &app.library.library);
    app.render_wanted = true;
}

static void run_rescan(void)
{
    cancel_edits();
    flush_listening();
    show_working_notice(STR_SCANNING, i18n_text(STR_SCAN_STARTING), WIRE_SHAPE_DISC);
    RescanProgress progress = {show_rescan_progress};
    RescanOutcome outcome = library_rescan_run(progress, app.rescan_stops);
    app.rescan_stops = 0;
    static char message[TOAST_TEXT_CAPACITY];
    const char *text = library_rescan_message(outcome, message, sizeof message);
    if (outcome.result == SCAN_OK) {
        music_check_remember(walk_music());
        start_app(false);
        app.covers_pending = true;
        app.covers_album = 0;
    } else {
        debug_loop_event(DEBUG_EVENT_STORAGE, DEBUG_STORAGE_SCAN_FAILED + (uint32_t)outcome.result);
    }
    app_wake();
    toast_show(&app.toast, text, hal_clock_milliseconds());
}

void app_release_audio(void)
{
    cancel_edits();
    flush_listening();
    spoken_store_flush(&app.spoken, player_status(&app.player), hal_clock_milliseconds());
    player_release_output(&app.player);
}

void app_storage_formatted(bool success)
{
    app.storage_formatted = true;
    app.format_success = success;
    app.listening_loaded = false;
}

AppDacSettings app_dac_settings(void)
{
    const Settings *settings = &app.settings.settings;
    return (AppDacSettings){settings->dac_volume, (uint8_t)settings->gain,
                            (uint8_t)settings->output, app.headphones.profiles.current};
}

const HeadphoneProfiles *app_headphone_profiles(void)
{
    return &app.headphones.profiles;
}

void app_remember_dac(AppDacSettings settings)
{
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

void app_open_usb_modes(void)
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
    const UsbChooser *usb = &app.screens.modules.settings.settings.usb;
    bool open = top_screen() == app.usb_chooser;
    bool lapsed =
        open && (usb->timed || app.usb_chooser_charging) && (usb->expired || !app.display_on);
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
        if (event.key == KEY_POWER && event.gesture == KEY_GESTURE_LONG_PRESS) {
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

static void show_prepare_progress(size_t index)
{
    char text[NOTICE_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_PREPARING_FILE, (uint32_t)index + 1,
                       (uint32_t)card_files_count());
    app_show_notice(i18n_text(STR_PREPARING_STORAGE), text);
}

static bool prepare_step(void)
{
    while (app.prepare_index < card_files_count()) {
        size_t index = app.prepare_index++;
        if (!card_files_missing(index)) {
            continue;
        }
        bool slow = card_files_slow(index);
        if (slow && !app.charging) {
            show_prepare_progress(index);
        }
        card_files_reserve(&app.prepare_report, index);
        if (slow) {
            return !app.charging;
        }
    }
    app.prepare_pending = false;
    if (app.headphones.load == STORE_UNREADABLE) {
        headphone_store_load(&app.headphones);
    }
    report_card_files(app.prepare_report, hal_clock_milliseconds());
    return false;
}

bool app_preparing(void)
{
    return app.prepare_pending;
}

static void start_debug_script(uint32_t now_ms)
{
    if (app.charging) {
        stop_charging(now_ms);
    }
    power_policy_wake(&app.power, now_ms);
}

static void set_debug_screen(bool on, uint32_t now_ms)
{
    if (on) {
        power_policy_wake(&app.power, now_ms);
    } else {
        power_policy_rest(&app.power);
    }
    debug_runner_done(true);
}

static bool save_debug_screenshot(const DebugRequest *request)
{
    DebugScreenshot shot = {.renderer = frame_renderer(whole_screen(), false),
                            .drawer = {.draw = draw_frame, .context = &app},
                            .name = request->argument,
                            .slot = request->slot};
    app.status_bar = current_status_bar();
    return debug_runner_screenshot(&shot);
}

static bool choose_setting(const DebugRequest *request, uint32_t now_ms)
{
    const SettingsModuleEntry *module = settings_module();
    bool chosen = module != NULL && module->choose != NULL &&
                  module->choose(&app.screens, request->argument, request->value);
    settle_screens(now_ms);
    return chosen;
}

static void run_debug_script(uint32_t now_ms)
{
    DebugRequest request =
        debug_runner_step(now_ms, (DebugRunnerView){fade_running(), app.display_on});
    if (request.kind == DEBUG_REQUEST_START) {
        start_debug_script(now_ms);
    } else if (request.kind == DEBUG_REQUEST_SCREEN) {
        set_debug_screen(request.slot != 0, now_ms);
    } else if (request.kind == DEBUG_REQUEST_SET) {
        debug_runner_done(choose_setting(&request, now_ms));
    } else if (request.kind == DEBUG_REQUEST_SCREENSHOT) {
        debug_runner_done(save_debug_screenshot(&request));
    } else if (request.kind == DEBUG_REQUEST_STANDBY) {
        player_hold_standby(&app.player, request.slot == 0);
        debug_runner_done(true);
    }
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

static uint32_t stamp_add(uint32_t stamp, uint32_t value)
{
    return hash_update(stamp, &value, sizeof value);
}

static uint32_t view_stamp(void)
{
    PlayerStatus status = player_status(&app.player);
    HalBattery battery = hal_power_battery();
    WallNow wall = wall_time_now();
    uint32_t stamp = HASH_START;
    stamp = stamp_add(stamp, (uint32_t)status.state);
    stamp = stamp_add(stamp, status.has_track);
    stamp = stamp_add(stamp, status.track);
    stamp = stamp_add(stamp, status.position_ms / VIEW_STAMP_POSITION_MS);
    stamp = stamp_add(stamp, status.duration_ms);
    stamp = stamp_add(stamp, status.volume);
    stamp = stamp_add(stamp, (uint32_t)status.shuffle);
    stamp = stamp_add(stamp, (uint32_t)status.repeat);
    stamp = stamp_add(stamp, status.place.number);
    stamp = stamp_add(stamp, status.place.total);
    stamp = stamp_add(stamp, app.settings.settings.status_items |
                                 (uint32_t)app.settings.settings.battery_percent << 8);
    stamp = stamp_add(stamp, battery.known);
    stamp = stamp_add(stamp, battery.percent);
    stamp = stamp_add(stamp, battery.charging);
    stamp = stamp_add(stamp, battery.usb_power);
    stamp = stamp_add(stamp, (uint32_t)bluetooth_of(&app.bluetooth));
    stamp = stamp_add(stamp, app.lock.locked);
    stamp = stamp_add(stamp, app.toast.visible);
    stamp = stamp_add(stamp, app.toast.until_ms);
    stamp = stamp_add(stamp, wall.known ? wall.local_s / VIEW_STAMP_MINUTE_S : 0);
    return stamp_add(stamp, (uint32_t)(uintptr_t)app.toast.text);
}

static bool view_changed(uint32_t now_ms)
{
    if (app.view_stamp_known && now_ms - app.view_checked_ms < VIEW_STAMP_CHECK_MS) {
        return false;
    }
    uint32_t stamp = view_stamp();
    bool changed = app.view_stamp_known && stamp != app.view_stamp;
    app.view_stamp_known = true;
    app.view_stamp = stamp;
    app.view_checked_ms = now_ms;
    return changed;
}

CORE_SEAM void app_step(void)
{
    if (app.powered_off) {
        return;
    }
    follow_compact_theme();
    if (app.prepare_pending && prepare_step()) {
        return;
    }
    if (!app.listening_loaded && !app.prepare_pending) {
        listening_store_load(&app.listening, &app.library.library);
        app.listening_loaded = true;
    }
    if (app.music_check_requested || app.rescan_manual) {
        app.music_check_requested = false;
        bool changed =
            hal_system_restart_pending() ||
            (app.rescan_manual && (music_check_firmware_is_new() || !music_check_settled()));
        if (!changed) {
            app_show_notice(i18n_text(STR_MUSIC), i18n_text(STR_CHECKING_MUSIC));
            changed = !music_check_unchanged(walk_music()) || !library_store_open(&app.library);
        }
        if (!changed) {
            music_check_note_firmware();
        }
        if (!changed && app.rescan_manual) {
            toast_show(&app.toast, i18n_text(STR_LIBRARY_UP_TO_DATE), hal_clock_milliseconds());
        }
        app.rescan_manual = false;
        app.rescan_requested |= changed;
        app.rescan_stops = changed ? 0 : app.rescan_stops;
    }
    if (app.rescan_requested) {
        app.rescan_requested = false;
        run_rescan();
        return;
    }
    if (app.language_requested) {
        app.language_requested = false;
        change_language(hal_clock_milliseconds());
    }
    uint32_t started_us = hal_clock_microseconds();
    uint32_t now_ms = hal_clock_milliseconds();
    app.now_ms = now_ms;
    hal_audio_service();
    hal_bluetooth_service();
    run_debug_script(now_ms);
    if (!app.charging) {
        continue_mixtape(now_ms);
        settle_screens(now_ms);
    }
    read_input();
    read_headset();
    key_tracker_update(&app.keys, now_ms);
    if (key_lock_update(&app.lock, now_ms)) {
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
        dispatch_key_events(now_ms);
    }
    if (app.storage_formatted) {
        bool success = app.format_success;
        app.storage_formatted = false;
        app.rescan_requested = false;
        app.music_check_requested = false;
        start_app(false);
        toast_show(&app.toast, i18n_text(success ? STR_CARD_FORMAT_DONE : STR_CARD_FORMAT_FAILED),
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
        spoken_store_follow(&app.spoken, player_status(&app.player), now_ms);
        explain_refusals(now_ms);
        debug_loop_follow(now_ms, &app.player);
        update_power(now_ms);
    }
    if (app.powered_off || app.charging) {
        return;
    }
    if (!app.display_on) {
        use_wanted_clock();
    }
    toast_update(&app.toast, now_ms);
    explain_lowered_volume(now_ms);
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
    render_when_due(now_ms, started_us);
    wire_motion_end(&app.motion, &app.notice);
    FrameDrawer drawer = {.draw = draw_frame, .context = &app};
    animate(drawer, top_screen(), app.display_on && !fade_running() && !slide_running(), now_ms);
}

static void render_when_due(uint32_t now_ms, uint32_t started_us)
{
    app.render_wanted |= view_changed(now_ms);
    bool urgent = app.render_urgent || app.light_after_frame || fade_running() || slide_running();
    if (!app.display_on || !frame_pacer_due(&app.pacer, now_ms, urgent || app.render_wanted)) {
        return;
    }
    app.render_urgent = false;
    app.render_wanted = false;
    frame_stats_record(&app.update_stats, hal_clock_microseconds() - started_us);
    app.frame_sent = false;
    render_frame();
    frame_pacer_done(&app.pacer, now_ms, app.frame_sent, urgent);
    light_after_first_frame();
    frame_stats_record(&app.frame_stats, hal_clock_microseconds() - started_us);
    debug_runner_frame(
        (DebugFrameSources){&app.frame_stats, &app.update_stats, &app.draw_stats, &app.send_stats});
}

bool app_charging(void)
{
    return app.charging;
}

uint32_t app_pass_interval_ms(void)
{
    bool busy = app.bluetooth.saved.enabled || app.pacer.fast_frames > 0 || app.render_urgent ||
                app.render_wanted || fade_running() || slide_running() || app.charging ||
                app.prepare_pending || app.rescan_requested || app.music_check_requested ||
                app.rescan_manual;
    if (busy) {
        return PASS_BUSY_MS;
    }
    bool playing = player_status(&app.player).state == PLAYER_PLAYING;
    return app.display_on || playing ? PASS_SCREEN_ON_MS : PASS_SCREEN_OFF_MS;
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
    render((FrameDrawer){.draw = draw_usb_status, .context = &frame}, false);
    light_after_first_frame();
}

CORE_SEAM void app_show_usb_area(AppUsbStatus status, Rect area)
{
    UsbStatusFrame frame = {.status = status, .battery = hal_power_battery()};
    StripRenderer renderer = frame_renderer(area, false);
    strip_renderer_render(&renderer, (FrameDrawer){.draw = draw_usb_status, .context = &frame},
                          (FrameSink){.write = send_motion, .context = NULL});
    hal_display_present();
    frame_diff_mask(&app.frame_diff, area);
    frame_diff_mask(&app.frame_diff, (Rect){0, 0, 0, 0});
}

CORE_SEAM void app_show_notice(const char *title, const char *message)
{
    bool working = app.motion.running && app.motion.owner == &app.notice;
    MessageConfig config = {.title = title,
                            .message = message,
                            .theme = &notice_theme,
                            .motion = working ? &app.motion : NULL};
    Screen *screen = message_screen_init(&app.notice, config);
    if (!app.display_on) {
        set_display(true);
    }
    render((FrameDrawer){.draw = draw_notice, .context = screen}, false);
    light_after_first_frame();
}

void app_show_splash(void)
{
    app.now_ms = hal_clock_milliseconds();
    fade_start(&app.splash, app.now_ms, SPLASH_TIMING);
}

void app_wake(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    power_policy_wake(&app.power, now_ms);
    if (app.charging) {
        light_charge_screen(now_ms);
    }
}

const FrameStats *app_frame_stats(void)
{
    return &app.frame_stats;
}

_Static_assert((int)DEBUG_FRAME_PARTS == (int)DRAW_PART_COUNT, "the report names every part");

DebugFrames app_debug_frames(void)
{
    DebugFrames frames = {.count = app.frame_stats.recorded,
                          .total_us = frame_stats_average_us(&app.frame_stats),
                          .worst_us = frame_stats_worst_us(&app.frame_stats),
                          .update_us = frame_stats_average_us(&app.update_stats),
                          .draw_us = frame_stats_average_us(&app.draw_stats),
                          .send_us = frame_stats_average_us(&app.send_stats)};
    for (int part = 0; part < DRAW_PART_COUNT; part++) {
        frames.part_us[part] = draw_profile_average_us((DrawPart)part);
    }
    return frames;
}

const char *app_screen_title(void)
{
    return top_screen()->title;
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
    if (screen_modules_use(&app.screens, SCREEN_MODULE_VIEWS) == NULL) {
        return NULL;
    }
    const MenuItem *item = &app.screens.modules.views.diagnostics_items[index];
    return item->value != NULL ? item->value(item->context) : NULL;
}
