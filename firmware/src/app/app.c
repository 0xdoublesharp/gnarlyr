#include "app/app.h"
#include "core/seam.h"

#include <stddef.h>

#include "app_internal.h"
#include "app_screens.h"
#include "bluetooth_link.h"
#include "board_layout.h"
#include "card_files.h"
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
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/power.h"
#include "language.h"
#include "library_rescan.h"
#include "library_store.h"
#include "music_check.h"
#include "player.h"
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

enum {
    PASS_BUSY_MS = 1,
    PASS_SCREEN_ON_MS = 4,
    PASS_SCREEN_OFF_MS = 10,
    VIEW_STAMP_CHECK_MS = 50,
    VIEW_STAMP_POSITION_MS = 1000,
    RESCAN_PROGRESS_MS = 250,
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
    SettingsStore settings;
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
    bool language_requested;
    bool music_check_requested;
    bool prepare_pending;
    size_t prepare_index;
    CardFilesReport prepare_report;
    bool light_after_frame;
    StringId goodbye_text;
    Toast toast;
    BluetoothLink bluetooth;
    uint32_t shown_refusals;
    StatusBarState status_bar;
    AppScreens screens;
    MessageScreen notice;
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
    uint32_t charge_lit_until_ms;
    bool volume_changed;
    KeyLock lock;
    uint32_t volume_changed_ms;
    HalBattery charge_battery;
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

static Screen *top_screen(void)
{
    return screen_stack_top(&app.stack);
}

static Rect whole_screen(void)
{
    const ThemeMetrics *metrics = &app.theme.metrics;
    return (Rect){0, 0, metrics->screen_width, metrics->screen_height};
}

static void apply_orientation(void)
{
    Orientation orientation = app.settings.settings.orientation;
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

static void save_session(void)
{
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

static void stop_charging(uint32_t now_ms)
{
    app.charging = false;
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
    return top_screen() == &app.screens.up_next.screen
               ? screen_action_pop()
               : screen_action_push(&app.screens.now_playing.screen);
}

static void start_slide(Screen *leaving, int depth_before, uint32_t now_ms)
{
    if (top_screen() == leaving) {
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

static void dispatch_to_screen(KeyEvent event, uint32_t now_ms)
{
    Screen *screen = top_screen();
    int depth_before = screen_stack_depth(&app.stack);
    ScreenAction action = opens_now_playing(event)
                              ? now_playing_action()
                              : screen->handle_key(screen->context, event, now_ms);
    if (screen_stack_apply(&app.stack, action, now_ms)) {
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
        screen_stack_apply(&app.stack, screen_action_push(&app.screens.now_playing.screen),
                           now_ms)) {
        screen_changed();
    }
}

static void run_player_command(NowPlayingCommand command)
{
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
        if (!app.powered_off && !app.charging && !fade_running()) {
            run_player_command(HEADSET_COMMANDS[command]);
        }
    }
}

static bool ignored_power_tap(KeyEvent event)
{
    return app.options.ignore_power_taps && event.key == KEY_POWER &&
           event.gesture == KEY_GESTURE_PRESS;
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
        debug_runner_active()) {
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
    return screen == &app.screens.home.screen || screen == &app.screens.now_playing.screen;
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
        status_bar_draw(canvas, board_theme(), state->status_bar);
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

static StatusBarState current_status_bar(void)
{
    HalBattery battery = hal_power_battery();
    return (StatusBarState){
        .playback = playback_of(player_status(&app.player).state),
        .bluetooth = bluetooth_of(&app.bluetooth),
        .battery_known = battery.known,
        .battery_percent = battery.percent,
        .usb_power = battery.usb_power,
        .locked = app.lock.locked,
        .shuffle = (StatusShuffle)player_status(&app.player).shuffle,
    };
}

static StripRenderer frame_renderer(bool drawer_paints_everything)
{
    return (StripRenderer){
        .strip_pixels = strip_pixels,
        .screen_width = app.theme.metrics.screen_width,
        .screen_height = app.theme.metrics.screen_height,
        .strip_height =
            (int)(sizeof strip_pixels / sizeof strip_pixels[0]) / app.theme.metrics.screen_width,
        .background = BOARD_THEME.colors.background,
        .drawer_paints_everything = drawer_paints_everything,
    };
}

static void render(FrameDrawer drawer, bool drawer_paints_everything)
{
    StripRenderer renderer = frame_renderer(drawer_paints_everything);
    FrameSink sink = {.write = write_to_display, .context = NULL};
    strip_renderer_render(&renderer, drawer, sink);
    hal_display_present();
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

static void start_playback(void)
{
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
    default_theme_set_accent(&app.theme, app.settings.settings.accent);
    default_theme_set_text_size(&app.theme, app.settings.settings.text_size);
    if (app.settings.settings.orientation != app.shown_orientation) {
        apply_orientation();
    }
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
        screen_stack_apply(&app.stack, screen_action_push(path[index]), now_ms);
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
        .favorites = &app.favorites,
        .power = &app.power,
        .toast = &app.toast,
        .covers = &app.covers,
        .rescan_requested = &app.rescan_requested,
        .language_requested = &app.language_requested,
        .bluetooth = &app.bluetooth,
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
    screen_stack_apply(&app.stack, screen_action_push(app_screens_language_row(&app.screens)),
                       now_ms);
    use_screen_key_behaviors();
    if (!language_is(settings->language)) {
        StringBuilder code = string_builder_make(settings->language, sizeof settings->language);
        string_builder_append(&code, i18n_code());
        save_settings();
        toast_show(&app.toast, i18n_text(STR_LANGUAGE_FAILED), now_ms);
    }
}

static void report_card_files(CardFilesReport files, uint32_t now_ms)
{
    if (files.card_full) {
        toast_show(&app.toast, i18n_text(STR_STORAGE_FULL), now_ms);
        debug_loop_event(DEBUG_EVENT_STORAGE, DEBUG_STORAGE_FULL);
    }
    if (files.library_created) {
        app.rescan_requested = true;
    } else if (music_check_firmware_is_new()) {
        app.music_check_requested = true;
    }
}

void app_start(void)
{
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
    app.theme = BOARD_THEME;
    apply_orientation();
    follow_appearance();
    start_playback();
    start_hardware(now_ms);
    bluetooth_link_resume(&app.bluetooth, &app.toast, now_ms);
    start_screens(now_ms);
    if (hal_power_take_usb_start()) {
        start_charging(now_ms);
    }
}

static void show_rescan_progress(uint32_t files, uint32_t tracks)
{
    static uint32_t shown_ms;
    uint32_t now_ms = hal_clock_milliseconds();
    hal_bluetooth_service();
    if (files > 1 && now_ms - shown_ms < RESCAN_PROGRESS_MS) {
        return;
    }
    shown_ms = now_ms;
    char text[NOTICE_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_SCAN_TRACKS, tracks, 0);
    app_show_notice(i18n_text(STR_SCANNING), text);
}

static void show_cover_progress(uint32_t done, uint32_t total)
{
    static uint32_t shown_ms;
    uint32_t now_ms = hal_clock_milliseconds();
    hal_bluetooth_service();
    if (done > 0 && done < total && now_ms - shown_ms < RESCAN_PROGRESS_MS) {
        return;
    }
    shown_ms = now_ms;
    char text[NOTICE_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_COVERS_DONE, done, total);
    app_show_notice(i18n_text(STR_FINDING_COVERS), text);
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

static void run_rescan(void)
{
    app_show_notice(i18n_text(STR_SCANNING), i18n_text(STR_SCAN_STARTING));
    RescanProgress progress = {show_rescan_progress, show_cover_progress};
    RescanOutcome outcome = library_rescan_run(progress);
    static char message[TOAST_TEXT_CAPACITY];
    const char *text = library_rescan_message(outcome, message, sizeof message);
    if (outcome.result == SCAN_OK) {
        music_check_remember(music_check_fingerprint());
        app_start();
    } else {
        debug_loop_event(DEBUG_EVENT_STORAGE, DEBUG_STORAGE_SCAN_FAILED + (uint32_t)outcome.result);
    }
    app_wake();
    toast_show(&app.toast, text, hal_clock_milliseconds());
}

void app_release_audio(void)
{
    spoken_store_flush(&app.spoken, player_status(&app.player), hal_clock_milliseconds());
    player_release_output(&app.player);
}

void app_remember_dac_volume(uint8_t level)
{
    app.settings.settings.dac_volume = level;
    settings_store_save(&app.settings);
}

void app_request_rescan(void)
{
    app.rescan_requested = true;
}

void app_request_rescan_if_music_changed(void)
{
    app.music_check_requested = true;
}

static void draw_charge(void *context, Canvas *canvas)
{
    (void)context;
    charge_screen_draw(canvas, board_theme(), app.charge_battery);
}

static void follow_charge_keys(uint32_t now_ms)
{
    KeyEvent event;
    while (app.charging && key_tracker_next_event(&app.keys, &event)) {
        if (event.key == KEY_POWER && event.gesture == KEY_GESTURE_LONG_PRESS) {
            stop_charging(now_ms);
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
        app.charge_battery = hal_power_battery();
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
    if (app.display_on) {
        render((FrameDrawer){.draw = draw_charge, .context = NULL}, true);
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
    DebugScreenshot shot = {.renderer = frame_renderer(false),
                            .drawer = {.draw = draw_frame, .context = &app},
                            .name = request->argument,
                            .slot = request->slot};
    app.status_bar = current_status_bar();
    return debug_screenshot_save(&shot);
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
        debug_runner_done(
            settings_screens_choose(&app.screens.settings, request.argument, request.value));
    } else if (request.kind == DEBUG_REQUEST_SCREENSHOT) {
        debug_runner_done(save_debug_screenshot(&request));
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
    stamp = stamp_add(stamp, battery.known);
    stamp = stamp_add(stamp, battery.percent);
    stamp = stamp_add(stamp, battery.charging);
    stamp = stamp_add(stamp, battery.usb_power);
    stamp = stamp_add(stamp, (uint32_t)bluetooth_of(&app.bluetooth));
    stamp = stamp_add(stamp, app.lock.locked);
    stamp = stamp_add(stamp, app.toast.visible);
    stamp = stamp_add(stamp, app.toast.until_ms);
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
    if (app.music_check_requested) {
        app.music_check_requested = false;
        bool changed = hal_system_restart_pending();
        if (!changed) {
            app_show_notice(i18n_text(STR_MUSIC), i18n_text(STR_CHECKING_MUSIC));
            changed = !music_check_unchanged() || !library_store_open(&app.library);
        }
        if (!changed) {
            music_check_note_firmware();
        }
        app.rescan_requested |= changed;
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
    if (app.saying_goodbye && fade_finished(&app.goodbye, now_ms)) {
        finish_shut_down();
    }
    if (!app.powered_off) {
        player_update(&app.player, now_ms);
        bluetooth_link_step(&app.bluetooth, &app.player, now_ms);
        spoken_store_follow(&app.spoken, player_status(&app.player), now_ms);
        explain_refusals(now_ms);
        debug_loop_follow(now_ms, &app.player);
        update_power(now_ms);
    }
    if (app.powered_off) {
        return;
    }
    if (!app.display_on) {
        use_wanted_clock();
    }
    toast_update(&app.toast, now_ms);
    follow_appearance();
    update_screen(now_ms);
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
                app.prepare_pending || app.rescan_requested || app.music_check_requested;
    if (busy) {
        return PASS_BUSY_MS;
    }
    bool playing = player_status(&app.player).state == PLAYER_PLAYING;
    return app.display_on || playing ? PASS_SCREEN_ON_MS : PASS_SCREEN_OFF_MS;
}

CORE_SEAM void app_show_notice(const char *title, const char *message)
{
    MessageConfig config = {.title = title, .message = message, .theme = &notice_theme};
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
    const MenuItem *item = &app.screens.diagnostics_items[index];
    return item->value != NULL ? item->value(item->context) : NULL;
}
