#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <execinfo.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "core/i18n.h"
#include "equalizer_presets.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/audio.h"
#include "hal/system.h"
#include "headphone_store.h"
#include "home_art.h"
#include "known_issues.h"
#include "module_guard.h"
#include "music_record.h"
#include "parametric_store.h"
#include "player.h"
#include "screen_modules.h"
#include "screens/usb_dac_menu.h"
#include "settings/equalizer_slots.h"
#include "settings/headphone_file.h"
#include "settings/parametric_file.h"
#include "settings_store.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define LIFECYCLE_ASAN 1
#endif
#endif

enum {
    OP_LIMIT = 96,
    TAP_MS = 30,
    LONG_MS = 1000,
    STEP_MS = 50,
    SETTLE_STEPS = 3,
    HOME_SETTINGS_ROW = 5,
    SETTINGS_ROWS = 48,
    DAC_KEY_LIMIT = 8,
    SLEEP_JUMP_MS = 60 * 1000,
    SLEEP_JUMPS = 17,
    FAILING_FILES = 5
};

typedef enum Op {
    OP_TAP,
    OP_TAP_AGAIN,
    OP_HOLD,
    OP_CHORD,
    OP_HOLD_MENU,
    OP_STORAGE_SESSION,
    OP_DAC_SESSION,
    OP_CABLE,
    OP_WRITE_FAILURE,
    OP_DAMAGE_MODULE,
    OP_WAIT,
    OP_SLEEP_EXPIRY,
    OP_SCREEN,
    OP_SETTING_ROW,
    OP_RESCAN,
    OP_ENVIRONMENT,
    OP_COUNT
} Op;

typedef struct Cursor {
    const uint8_t *data;
    size_t size;
    size_t at;
} Cursor;

typedef struct Expected {
    bool blocked;
    uint8_t slots_used;
    bool graphic_known;
    bool parametric_known;
} Expected;

static const uint32_t MODULES[] = {
    SCREEN_MODULE_VIEWS,     SCREEN_MODULE_SETTINGS, SCREEN_MODULE_LIBRARY, SCREEN_MODULE_LISTENING,
    SCREEN_MODULE_EQUALIZER, SCREEN_MODULE_PLAYING,  SCREEN_MODULE_STAGE};

static const char *failing_paths[FAILING_FILES];
static bool failing[FAILING_FILES];
static uint32_t broken_modules;
static uint32_t now_ms;
static uint8_t shortcut;
static bool cable;
static Expected expected;

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t next_byte(Cursor *cursor)
{
    return cursor->at < cursor->size ? cursor->data[cursor->at++] : 0;
}

static bool tracing(void)
{
    static int known = -1;
    if (known < 0) {
        known = getenv("ECHO_LIFECYCLE_TRACE") != NULL ? 1 : 0;
    }
    return known == 1;
}

static void violated(const char *invariant)
{
    fprintf(stderr, "lifecycle invariant failed: %s (screen \"%s\", module %s)\n", invariant,
            app_screen_title() != NULL ? app_screen_title() : "?",
            module_guard_module_name(fake_hal_loaded_module()));
    abort();
}

static void step(void)
{
    now_ms += STEP_MS;
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void settle(int steps)
{
    for (int index = 0; index < steps; index++) {
        step();
    }
}

static void press(Key key, uint32_t duration_ms)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + duration_ms);
    settle((int)(duration_ms / STEP_MS) + 2);
}

static void chord(Key first, Key second, uint32_t duration_ms)
{
    fake_hal_queue_transition(first, true, now_ms);
    fake_hal_queue_transition(second, true, now_ms + 40);
    fake_hal_queue_transition(second, false, now_ms + 40 + duration_ms);
    fake_hal_queue_transition(first, false, now_ms + 80 + duration_ms);
    settle((int)(duration_ms / STEP_MS) + 4);
}

static bool read_internal(const char *path, uint32_t offset, void *bytes, uint32_t length)
{
    uint32_t size = 0;
    const uint8_t *stored = fake_hal_internal_file_bytes(path, &size);
    if (stored == NULL || offset > size || length > size - offset) {
        return false;
    }
    memcpy(bytes, stored + offset, length);
    return true;
}

static bool read_headphones(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    return read_internal(HEADPHONE_STORE_PATH, offset, bytes, length);
}

static bool read_parametric(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    return read_internal(PARAMETRIC_STORE_PATH, offset, bytes, length);
}

static uint32_t stored_size(const char *path)
{
    uint32_t size = 0;
    return fake_hal_internal_file_bytes(path, &size) != NULL ? size : 0;
}

static HeadphoneProfiles stored_profiles(void)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    HeadphoneFile file;
    StoreFile source = {.source = {read_headphones, NULL},
                        .size = stored_size(HEADPHONE_STORE_PATH)};
    if (headphone_file_open(&file, source, &profiles) != STORE_READY) {
        profiles = headphone_profiles_defaults();
    }
    return profiles;
}

static bool same_profiles(const HeadphoneProfiles *left, const HeadphoneProfiles *right)
{
    if (left->current != right->current || right->count < left->count) {
        return false;
    }
    for (int index = 0; index < left->count && index < HEADPHONE_PROFILE_LIMIT; index++) {
        const HeadphoneProfile *a = &left->slots[index];
        const HeadphoneProfile *b = &right->slots[index];
        if (a->volume_ceiling != b->volume_ceiling || a->trim_half_db != b->trim_half_db ||
            a->balance != b->balance || strcmp(a->name, b->name) != 0) {
            return false;
        }
    }
    return true;
}

static void check_headphones(void)
{
    HeadphoneAudio applied = hal_audio_headphones();
    if (expected.blocked) {
        if (applied.ceiling != 0 && !known_issue_open(KNOWN_ISSUE_BLOCKED_PROFILE_CEILING)) {
            fprintf(stderr, "applied ceiling %d\n", applied.ceiling);
            violated("the applied ceiling is not 0 while the profile store is blocked");
        }
        return;
    }
    HeadphoneProfiles stored = stored_profiles();
    const HeadphoneProfiles *memory = app_headphone_profiles();
    if (!same_profiles(&stored, memory)) {
        fprintf(stderr, "stored count %d current %d, memory count %d current %d\n", stored.count,
                stored.current, memory->count, memory->current);
        violated("the headphone profiles in memory differ from headphones.bin");
    }
    if (applied.ceiling > headphone_store_parameters(&stored).ceiling) {
        violated("the applied volume ceiling exceeds the stored profile's");
    }
}

static bool stored_graphic(EqualizerSlots *slots)
{
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_internal_file_bytes(EQUALIZER_SLOTS_PATH, &size);
    module_guard_oracle_begin(SCREEN_MODULE_EQUALIZER);
    bool decoded = bytes != NULL && equalizer_slots_decode(bytes, size, slots);
    module_guard_oracle_end();
    return decoded;
}

static bool stored_parametric(ParametricBank *bank)
{
    ParametricFile file;
    StoreFile source = {.source = {read_parametric, NULL},
                        .size = stored_size(PARAMETRIC_STORE_PATH)};
    module_guard_oracle_begin(SCREEN_MODULE_EQUALIZER);
    StoreLoad load = parametric_file_open(&file, source, bank);
    module_guard_oracle_end();
    return load == STORE_READY;
}

static void check_slots(void)
{
    EqualizerSlots graphic = {0};
    ParametricBank bank = {0};
    bool graphic_read = stored_graphic(&graphic);
    bool parametric_read = stored_parametric(&bank);
    if ((expected.graphic_known && !graphic_read) ||
        (expected.parametric_known && !parametric_read)) {
        violated("a stored EQ slot file no longer loads");
    }
    uint8_t used = (uint8_t)((graphic_read ? graphic.used : 0) | (parametric_read ? bank.used : 0));
    if ((used & expected.slots_used) != expected.slots_used) {
        violated("a saved EQ slot was lost");
    }
    expected.slots_used = used;
    expected.graphic_known = graphic_read;
    expected.parametric_known = parametric_read;
}

static void check_invariants(void)
{
    if (app_screen_title() == NULL || app_screen_depth() <= 0) {
        violated("the screen stack is empty");
    }
    PlayerStatus status = app_player_status();
    if (status.volume > HAL_AUDIO_VOLUME_MAX) {
        violated("the player volume exceeds the hardware range");
    }
    check_headphones();
    check_slots();
}

static void storage_session(uint8_t argument)
{
    cable = true;
    fake_hal_set_usb_cable(true);
    app_release_audio();
    fake_hal_load_usb_overlay();
    app_set_usb_renderer(NULL);
    if ((argument & 1u) != 0) {
        app_leave_storage();
        app_start_after_storage();
        app_request_rescan_if_music_changed();
    }
    app_resume_after_usb();
    if ((argument & 2u) != 0) {
        app_open_usb_modes();
    }
    settle(SETTLE_STEPS);
}

static void dac_switch_headphones(uint8_t chosen)
{
    HeadphoneProfiles profiles = *app_headphone_profiles();
    profiles.current = chosen;
    hal_audio_set_headphones(headphone_store_parameters(&profiles));
}

static bool dac_apply(UsbDacMenu *menu, AppDacSettings *settings, UsbDacAction action)
{
    settings->output = menu->settings.output;
    settings->headphones = menu->settings.headphones;
    if (action == USB_DAC_ACTION_HEADPHONES) {
        dac_switch_headphones(settings->headphones);
    } else if (action == USB_DAC_ACTION_VOLUME) {
        settings->volume = headphone_audio_limit(hal_audio_headphones(), menu->settings.volume);
    } else if (action == USB_DAC_ACTION_GAIN) {
        settings->gain = menu->settings.gain;
    }
    return action != USB_DAC_ACTION_CLOSE && action != USB_DAC_ACTION_COMPUTER &&
           action != USB_DAC_ACTION_CHARGE;
}

static void dac_session(Cursor *cursor, uint8_t argument)
{
    static UsbDacMenu menu;
    cable = true;
    fake_hal_set_usb_cable(true);
    app_release_audio();
    fake_hal_load_usb_overlay();
    AppDacSettings before = app_dac_settings();
    AppDacSettings settings = before;
    usb_dac_menu_open(&menu, (UsbDacMenuConfig){app_theme(), settings, app_headphone_profiles()},
                      now_ms);
    int keys = argument % DAC_KEY_LIMIT;
    for (int index = 0; index < keys; index++) {
        Key key = (Key)(next_byte(cursor) % (KEY_BACK + 1));
        if (!dac_apply(&menu, &settings, usb_dac_menu_key(&menu, key, now_ms))) {
            break;
        }
    }
    if (memcmp(&settings, &before, sizeof settings) != 0) {
        app_remember_dac(settings);
    }
    app_set_usb_renderer(NULL);
    app_resume_after_usb();
    settle(SETTLE_STEPS);
}

static void toggle_write_failure(uint8_t argument)
{
    int choice = argument % (FAILING_FILES + 2);
    for (int index = 0; index < FAILING_FILES; index++) {
        bool on = choice == FAILING_FILES + 1 ? true
                  : choice == FAILING_FILES   ? false
                  : index == choice           ? !failing[index]
                                              : failing[index];
        failing[index] = on;
        fake_hal_fail_internal_writes_at(index, on ? failing_paths[index] : NULL);
    }
}

static void damage_module(uint8_t argument)
{
    uint32_t module = MODULES[argument % (sizeof MODULES / sizeof MODULES[0])];
    broken_modules ^= 1u << module;
    fake_hal_break_module(module, (broken_modules & 1u << module) != 0);
}

static void open_setting_row(uint8_t row)
{
    press(KEY_BACK, LONG_MS);
    int home_row = HOME_SETTINGS_ROW + (app_player_status().has_track ? 1 : 0);
    for (int index = 0; index < home_row; index++) {
        press(KEY_VOLUME_UP, TAP_MS);
    }
    press(KEY_PLAY, TAP_MS);
    for (int index = 0; index < row % SETTINGS_ROWS; index++) {
        press(KEY_VOLUME_UP, TAP_MS);
    }
    press(KEY_PLAY, TAP_MS);
}

static void apply_environment(uint8_t value)
{
    switch (value % 6) {
    case 0:
        fake_hal_audio_finish_track();
        break;
    case 1:
        fake_hal_audio_fail_track();
        break;
    case 2:
        fake_hal_audio_set_jack((HalAudioJack)(value % (HAL_AUDIO_JACK_BALANCED + 1)));
        break;
    case 3:
        fake_hal_queue_headset_command((HalHeadsetCommand)(value % (HAL_HEADSET_VOLUME_DOWN + 1)));
        break;
    case 4:
        fake_hal_set_battery((HalBattery){
            .percent = value % 101, .usb_power = cable, .charging = cable && (value & 1u) != 0});
        break;
    default:
        app_request_rescan();
        break;
    }
    settle(SETTLE_STEPS);
}

static bool hold_menu_allowed(void)
{
    uint32_t loaded = fake_hal_loaded_module();
    uint32_t needed = shortcut == 3 ? SCREEN_MODULE_VIEWS : SCREEN_MODULE_SETTINGS;
    return !known_issue_open(KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE) || shortcut == 0 || loaded == 0 ||
           loaded == needed;
}

static void run_input_op(Op op, uint8_t argument)
{
    Key key = (Key)(argument % KEY_COUNT);
    switch (op) {
    case OP_TAP:
    case OP_TAP_AGAIN:
        press(key, TAP_MS);
        break;
    case OP_HOLD:
        if (key != KEY_MENU || hold_menu_allowed()) {
            press(key, LONG_MS);
        }
        break;
    case OP_CHORD:
        chord((argument & 8u) != 0 ? KEY_MENU : KEY_PLAY,
              (argument & 1u) != 0 ? KEY_VOLUME_DOWN : KEY_VOLUME_UP,
              (argument & 2u) != 0 ? LONG_MS : TAP_MS);
        break;
    case OP_HOLD_MENU:
        if (hold_menu_allowed()) {
            press(KEY_MENU, LONG_MS);
        }
        break;
    case OP_SCREEN:
        press(KEY_POWER, TAP_MS);
        break;
    default:
        break;
    }
}

static bool title_is(StringId id)
{
    const char *title = app_screen_title();
    return title != NULL && strcmp(title, i18n_text(id)) == 0;
}

static bool on_equalizer(void)
{
    return title_is(STR_CUSTOM_EQ) || title_is(STR_EQUALIZER) || title_is(STR_PEQ);
}

static bool on_reload_issue_screen(void)
{
    return (known_issue_open(KNOWN_ISSUE_EQUALIZER_STATE_AFTER_RELOAD) && on_equalizer()) ||
           (known_issue_open(KNOWN_ISSUE_SAFE_START_LABELS_AFTER_RELOAD) &&
            title_is(STR_SAFE_START_VOLUME));
}

static bool evicts(Op op)
{
    return op == OP_STORAGE_SESSION || op == OP_DAC_SESSION || op == OP_RESCAN ||
           op == OP_ENVIRONMENT || op == OP_DAMAGE_MODULE || op == OP_SLEEP_EXPIRY;
}

static bool goes_home(Op op, uint8_t argument)
{
    return op == OP_SETTING_ROW || (op == OP_HOLD && argument % KEY_COUNT == KEY_BACK);
}

static bool allowed(Op op, uint8_t argument)
{
    if (evicts(op) && on_reload_issue_screen()) {
        return false;
    }
    return !(goes_home(op, argument) && known_issue_open(KNOWN_ISSUE_HOME_ART_AFTER_OVERLAY) &&
             fake_hal_loaded_module() == 0);
}

static void run_op(Cursor *cursor, Op op, uint8_t argument)
{
    if (!allowed(op, argument)) {
        return;
    }
    switch (op) {
    case OP_STORAGE_SESSION:
        storage_session(argument);
        break;
    case OP_DAC_SESSION:
        dac_session(cursor, argument);
        break;
    case OP_CABLE:
        cable = !cable;
        fake_hal_set_usb_cable(cable);
        settle(SETTLE_STEPS);
        break;
    case OP_WRITE_FAILURE:
        toggle_write_failure(argument);
        break;
    case OP_DAMAGE_MODULE:
        damage_module(argument);
        break;
    case OP_WAIT:
        settle(1 + argument * 5);
        break;
    case OP_SLEEP_EXPIRY:
        for (int jump = 0; jump < SLEEP_JUMPS; jump++) {
            now_ms += SLEEP_JUMP_MS;
            step();
        }
        break;
    case OP_SETTING_ROW:
        open_setting_row(next_byte(cursor));
        break;
    case OP_RESCAN:
        app_request_rescan();
        settle(SETTLE_STEPS);
        break;
    case OP_ENVIRONMENT:
        apply_environment(next_byte(cursor));
        break;
    default:
        run_input_op(op, argument);
        break;
    }
}

static void add_library_files(uint8_t control)
{
    static FixtureLibrary fixture;
    fixture_library_open(&fixture);
    uint32_t covers_size = 0;
    const uint8_t *covers = fixture_covers_bytes(&covers_size);
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", fixture.bytes, fixture.size);
    if ((control & 1u) == 0) {
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/covers.bin", covers, covers_size);
    }
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/build.tmp", NULL, 16 * 1024 * 1024);
    for (size_t index = 0; index < card_files_count(); index++) {
        if (card_files_slow(index)) {
            fake_hal_add_file(card_files_path(index), NULL, 4096);
        }
    }
    music_record_write(&(MusicRecord){.card = {.complete = true}});
}

static void seed_profiles(uint8_t config)
{
    HeadphoneStore store;
    (void)hal_internal_file_reserve(HEADPHONE_STORE_PATH, HEADPHONE_FILE_BYTES);
    headphone_store_load(&store);
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    headphone_store_fill_slots(&profiles, 0);
    profiles.count = 3;
    for (int index = 0; index < profiles.count; index++) {
        profiles.slots[index].volume_ceiling = (uint8_t)(12 + 12 * index);
    }
    profiles.current = (uint8_t)(config % 3);
    (void)headphone_file_save(&store.file, &profiles);
}

static void seed_equalizer(void)
{
    EqualizerSlots slots = {0};
    int8_t gains[SETTINGS_EQUALIZER_BANDS] = {0};
    gains[2] = 4;
    equalizer_slots_put(&slots, 1, gains);
    equalizer_slots_put(&slots, 4, gains);
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_encode(&slots, bytes);
    (void)hal_internal_file_write(EQUALIZER_SLOTS_PATH, bytes, sizeof bytes);
    ParametricStore store = {0};
    (void)hal_internal_file_reserve(PARAMETRIC_STORE_PATH, PARAMETRIC_FILE_BYTES);
    parametric_store_read(&store, NULL);
    ParametricBank bank = *parametric_store_bank();
    bank.slots[2].count = 1;
    bank.slots[2].preamp = -20;
    bank.used |= 1u << 2;
    (void)parametric_file_save(&store.file, &bank);
}

static void seed_settings(uint8_t config)
{
    SettingsStore store;
    settings_store_load(&store);
    shortcut = (uint8_t)(config % 6);
    store.settings.long_press_action = shortcut;
    if ((config & 8u) != 0) {
        settings_set_now_playing_style(&store.settings, NOW_PLAYING_VU);
    }
    (void)settings_store_save(&store);
    module_guard_oracle_begin(SCREEN_MODULE_EQUALIZER);
    if ((config & 16u) != 0) {
        seed_profiles(config >> 5);
    }
    if ((config & 32u) != 0) {
        seed_equalizer();
    }
    module_guard_oracle_end();
}

static void leave_known_issue_screens(void)
{
    bool library = known_issue_open(KNOWN_ISSUE_DISCOVERY_WORK_AFTER_RESCAN) &&
                   fake_hal_loaded_module() == SCREEN_MODULE_LIBRARY;
    if (library || on_reload_issue_screen()) {
        press(KEY_BACK, LONG_MS);
    }
}

static bool overlay_evicted(const char *report)
{
    return strstr(report, "the rescan overlay") != NULL ||
           strstr(report, "the USB overlay") != NULL;
}

static bool on_stack(const char *name)
{
    void *frames[64];
    int count = backtrace(frames, 64);
    for (int index = 0; index < count; index++) {
        Dl_info info;
        if (dladdr(frames[index], &info) != 0 && info.dli_sname != NULL &&
            strcmp(info.dli_sname, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool rendering(void)
{
    return on_stack("strip_renderer_render");
}

static bool cancelling_previews(void)
{
    return on_stack("screen_modules_cancel_previews") || on_stack("screen_modules_use");
}

static void guard_trap(const char *report, const void *function)
{
    if (!overlay_evicted(report) && known_issue_open(KNOWN_ISSUE_SLIDE_LEAVING_EVICTED) &&
        rendering()) {
        module_guard_lend(function);
        return;
    }
    bool art = function == (const void *)(uintptr_t)home_art_update ||
               strstr(report, "views+settings+library+listening+equalizer+playing+stage") != NULL;
    if (art && overlay_evicted(report) && known_issue_open(KNOWN_ISSUE_HOME_ART_AFTER_OVERLAY)) {
        module_guard_lend(function);
        return;
    }
    if (known_issue_open(KNOWN_ISSUE_PREVIEW_CANCEL_EVICTED) && cancelling_previews()) {
        module_guard_lend(function);
        return;
    }
    if (strstr(report, "for a Hold Menu shortcut") != NULL &&
        known_issue_open(KNOWN_ISSUE_HOLD_MENU_STALE_UPDATE)) {
        module_guard_lend(function);
        return;
    }
    fprintf(stderr, "screen \"%s\" at depth %d\n", app_screen_title(), app_screen_depth());
    module_guard_abort(report);
}

static void start(const uint8_t *data, size_t size)
{
    module_guard_restore_all();
    app_reset_for_test();
    fake_hal_reset();
    module_guard_set_trap(guard_trap);
    add_library_files(size > 0 ? data[0] : 0);
    now_ms = 1000;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    uint8_t config = size > 1 ? data[1] : 0;
    seed_settings(config);
    expected = (Expected){.blocked = (config & 64u) != 0 && (config & 16u) != 0};
    if (expected.blocked) {
        fake_hal_fail_internal_reads(HEADPHONE_STORE_PATH);
    }
    now_ms += 10;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    settle(SETTLE_STEPS);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    failing_paths[0] = SETTINGS_STORE_PATH;
    failing_paths[1] = SETTINGS_BUTTONS_PATH;
    failing_paths[2] = HEADPHONE_STORE_PATH;
    failing_paths[3] = EQUALIZER_SLOTS_PATH;
    failing_paths[4] = PARAMETRIC_STORE_PATH;
    fake_hal_reset();
    const void *probe = module_guard_probe(SCREEN_MODULE_SETTINGS);
    if (module_guard_guarded_sets() == 0 || probe == NULL) {
        fprintf(stderr, "lifecycle: the module guard has no module sections\n");
        abort();
    }
    (void)hal_system_load_module(SCREEN_MODULE_SETTINGS);
    (void)hal_system_load_module(SCREEN_MODULE_VIEWS);
    bool poisoned = module_guard_evicted(probe);
#if defined(LIFECYCLE_ASAN)
    poisoned = poisoned && __asan_address_is_poisoned(probe);
#endif
    (void)hal_system_load_module(SCREEN_MODULE_SETTINGS);
    if (!poisoned || module_guard_evicted(probe)) {
        fprintf(stderr, "lifecycle: eviction is not poisoning module memory\n");
        abort();
    }
    fake_hal_reset();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    start(data, size);
    memset(failing, 0, sizeof failing);
    broken_modules = 0;
    cable = false;
    Cursor cursor = {data, size, 2};
    check_invariants();
    for (int count = 0; count < OP_LIMIT && cursor.at < size; count++) {
        uint8_t value = next_byte(&cursor);
        if (tracing()) {
            fprintf(stderr, "op %d: kind %d argument %d at byte %zu, screen \"%s\"\n", count,
                    value % OP_COUNT, value / OP_COUNT, cursor.at - 1, app_screen_title());
        }
        run_op(&cursor, (Op)(value % OP_COUNT), (uint8_t)(value / OP_COUNT));
        leave_known_issue_screens();
        check_invariants();
    }
    app_stop();
    return 0;
}
