#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    OP_LIMIT = 48,
    STEP_BUDGET = 220,
    TAP_MS = 30,
    LONG_MS = 1000,
    STEP_MS = 50,
    SETTLE_STEPS = 3,
    POWER_ON_MS = 2000,
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
    SCREEN_MODULE_EQUALIZER, SCREEN_MODULE_PLAYING,  SCREEN_MODULE_STAGE,     SCREEN_MODULE_FORMAT};

static const char *failing_paths[FAILING_FILES];
static bool failing[FAILING_FILES];
static uint32_t broken_modules;
static int power_offs;
static int step_count;
static uint32_t now_ms;
static bool cable;
static Expected expected;

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t next_byte(Cursor *cursor)
{
    return cursor->at < cursor->size ? cursor->data[cursor->at++] : 0;
}

static int op_limit(void)
{
    static int limit;
    if (limit == 0) {
        const char *text = getenv("ECHO_LIFECYCLE_OPS");
        limit = text != NULL && atoi(text) > 0 ? atoi(text) : OP_LIMIT;
    }
    return limit;
}

static int step_budget(void)
{
    static int budget;
    if (budget == 0) {
        const char *text = getenv("ECHO_LIFECYCLE_STEPS");
        budget = text != NULL && atoi(text) > 0 ? atoi(text) : STEP_BUDGET;
    }
    return budget;
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
    step_count++;
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

static HeadphoneProfiles stored_profiles(StoreLoad *load)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    HeadphoneFile file;
    StoreFile source = {.source = {read_headphones, NULL},
                        .size = stored_size(HEADPHONE_STORE_PATH)};
    *load = headphone_file_open(&file, source, &profiles);
    if (*load != STORE_READY) {
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
        if (applied.ceiling != 0) {
            fprintf(stderr, "applied ceiling %d\n", applied.ceiling);
            violated("the applied ceiling is not 0 while the profile store is blocked");
        }
        return;
    }
    StoreLoad load;
    HeadphoneProfiles stored = stored_profiles(&load);
    if (load != STORE_READY && load != STORE_EMPTY && applied.ceiling != 0) {
        fprintf(stderr, "applied ceiling %d, stored profiles load %d\n", applied.ceiling, load);
        violated("the applied ceiling is not 0 while headphones.bin holds no readable profile");
    }
    const HeadphoneProfiles *memory = app_headphone_profiles();
    if (!same_profiles(&stored, memory)) {
        fprintf(stderr, "stored count %d current %d, memory count %d current %d\n", stored.count,
                stored.current, memory->count, memory->current);
        violated("the headphone profiles in memory differ from headphones.bin");
    }
    if (applied.ceiling > headphone_store_parameters(&stored).ceiling) {
        fprintf(stderr, "applied ceiling %d, stored profile %d of %d with ceiling %d\n",
                applied.ceiling, stored.current, stored.count,
                headphone_store_parameters(&stored).ceiling);
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
    if (tracing()) {
        fprintf(stderr, "slots: graphic %d %#x, parametric %d %#x\n", graphic_read, graphic.used,
                parametric_read, bank.used);
    }
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
    if (tracing()) {
        fprintf(stderr, "dac: headphones %d -> %d, ceiling applied %d, broken %#x, refusals %d\n",
                before.headphones, settings.headphones, hal_audio_headphones().ceiling,
                broken_modules, fake_hal_module_refusals());
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

static void quick_tap(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + TAP_MS);
    step();
}

static void open_setting_row(uint8_t row)
{
    press(KEY_BACK, LONG_MS);
    int home_row = HOME_SETTINGS_ROW + (app_player_status().has_track ? 1 : 0);
    for (int index = 0; index < home_row; index++) {
        quick_tap(KEY_VOLUME_UP);
    }
    press(KEY_PLAY, TAP_MS);
    for (int index = 0; index < row % SETTINGS_ROWS; index++) {
        quick_tap(KEY_VOLUME_UP);
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

static void run_input_op(Op op, uint8_t argument)
{
    Key key = (Key)(argument % KEY_COUNT);
    switch (op) {
    case OP_TAP:
    case OP_TAP_AGAIN:
        press(key, TAP_MS);
        break;
    case OP_HOLD:
        press(key, LONG_MS);
        break;
    case OP_CHORD:
        chord((argument & 8u) != 0 ? KEY_MENU : KEY_PLAY,
              (argument & 1u) != 0 ? KEY_VOLUME_DOWN : KEY_VOLUME_UP,
              (argument & 2u) != 0 ? LONG_MS : TAP_MS);
        break;
    case OP_HOLD_MENU:
        press(KEY_MENU, LONG_MS);
        break;
    case OP_SCREEN:
        press(KEY_POWER, TAP_MS);
        break;
    default:
        break;
    }
}

static void run_op(Cursor *cursor, Op op, uint8_t argument)
{
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

static void corrupt_profiles(void)
{
    uint8_t garbage[HEADPHONE_FILE_BYTES / 2];
    memset(garbage, 0x5A, sizeof garbage);
    (void)hal_internal_file_write_at(HEADPHONE_STORE_PATH, 0, garbage, sizeof garbage);
    (void)hal_internal_file_write_at(HEADPHONE_STORE_PATH, sizeof garbage, garbage, sizeof garbage);
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
    bank.slots[2].filters[0] = (ParametricFilter){1000, 71, 0, PARAMETRIC_PEAK, true};
    bank.used |= 1u << 2;
    if (parametric_file_save(&store.file, &bank) != STORE_DONE) {
        fprintf(stderr, "lifecycle: parametric seed not saved\n");
        abort();
    }
}

static void seed_settings(uint8_t config)
{
    SettingsStore store;
    settings_store_load(&store);
    store.settings.long_press_action = (uint8_t)(config % 6);
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

static void power_on_after_power_off(void)
{
    if (fake_hal_power_off_count() != power_offs) {
        power_offs = fake_hal_power_off_count();
        now_ms += POWER_ON_MS;
        fake_hal_set_milliseconds(now_ms);
        app_start();
        settle(SETTLE_STEPS);
    }
}

static void start(const uint8_t *data, size_t size)
{
    module_guard_restore_all();
    app_reset_for_test();
    fake_hal_reset();
    module_guard_set_trap(module_guard_trap_abort);
    add_library_files(size > 0 ? data[0] : 0);
    now_ms = 1000;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    uint8_t config = size > 1 ? data[1] : 0;
    seed_settings(config);
    expected = (Expected){.blocked = (config & 64u) != 0 && (config & 16u) != 0};
    if (expected.blocked) {
        fake_hal_fail_internal_reads(HEADPHONE_STORE_PATH);
    } else if ((config & 128u) != 0 && (config & 16u) != 0) {
        corrupt_profiles();
    }
    now_ms += 10;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    settle(SETTLE_STEPS);
    power_offs = fake_hal_power_off_count();
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
    step_count = 0;
    static Cursor cursor;
    cursor = (Cursor){data, size, 2};
    check_invariants();
    for (int count = 0; count < op_limit() && cursor.at < size && step_count < step_budget();
         count++) {
        uint8_t value = next_byte(&cursor);
        if (tracing()) {
            fprintf(stderr,
                    "op %d: kind %d argument %d at byte %zu, screen \"%s\", step %d, %ld ms\n",
                    count, value % OP_COUNT, value / OP_COUNT, cursor.at - 1, app_screen_title(),
                    step_count, (long)(clock() * 1000 / CLOCKS_PER_SEC));
        }
        run_op(&cursor, (Op)(value % OP_COUNT), (uint8_t)(value / OP_COUNT));
        power_on_after_power_off();
        check_invariants();
    }
    app_stop();
    return 0;
}
