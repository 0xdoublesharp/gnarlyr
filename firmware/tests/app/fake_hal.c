#include "fake_hal.h"
#include "app/debug_loop.h"
#include "app/extension.h"
#include "app/linked_modules.h"

#include "hal/system.h"

#include "app/rescan_module.h"
#include "core/i18n.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/language.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "playback/track_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TRANSITION_CAPACITY = 16,
    FILE_CAPACITY = 64,
    PROBE_CALL_CAPACITY = 32,
    WRITTEN_CAPACITY = 1100,
    PATH_CAPACITY = 260,
    INTERNAL_CAPACITY = 6,
    SCREENSHOT_CAPACITY = 32 + HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE * 2
};

typedef struct FakeAudio {
    HalAudioStatus status;
    char playing_path[PATH_CAPACITY];
    uint32_t play_start_ms;
    uint32_t play_end_ms;
    uint32_t queued_start_ms;
    uint32_t queued_end_ms;
    int play_count;
    int release_count;
    char queued_path[PATH_CAPACITY];
    bool has_queued;
    uint32_t queued_token;
    uint32_t replay_gain;
    uint32_t queued_replay_gain;
    uint32_t queued_fade_ms;
    uint32_t play_fade_ms;
    int seek_count;
    uint32_t last_seek_ms;
    uint8_t volume;
    HalAudioGain gain;
    HalAudioOutput output;
    HalAudioEqualizer equalizer;
    HeadphoneAudio headphones;
    int refused_plays;
    HalAudioJack jack;
    HalAudioRefusal refusal;
    DacIdentity dac_identity[DAC_IDENTITY_CHIPS];
} FakeAudio;

typedef struct WrittenFile {
    char path[PATH_CAPACITY];
    uint8_t bytes[WRITTEN_CAPACITY];
} WrittenFile;

typedef struct InternalFile {
    char path[PATH_CAPACITY];
    uint8_t bytes[WRITTEN_CAPACITY];
    uint32_t size;
} InternalFile;

typedef struct FakeFile {
    char path[PATH_CAPACITY];
    const uint8_t *bytes;
    uint32_t size;
    uint32_t stored;
} FakeFile;

enum {
    STORE_SLOTS = 12,
    STORE_CAPACITY = 512 * 1024
};

static uint8_t stores[STORE_SLOTS][STORE_CAPACITY];
typedef struct LargeStore {
    uint8_t *bytes;
    uint32_t size;
} LargeStore;

static LargeStore large_stores[2];

typedef struct FakeHal {
    const char *version;
    uint16_t panel[HAL_DISPLAY_LONG_SIDE * HAL_DISPLAY_SHORT_SIDE];
    HalDisplayRotation rotation;
    long pixels_written;
    FakeDisplayWrites writes;
    int present_count;
    bool display_on;
    uint8_t brightness;
    HalBattery battery;
    int power_off_count;
    HalAudioState audio_state_at_power_off;
    uint32_t now_ms;
    uint32_t now_us;
    uint32_t microseconds_per_read;
    HalKeyTransition transitions[TRANSITION_CAPACITY];
    int transition_count;
    int next_transition;
    HalHeadsetCommand headset_commands[TRANSITION_CAPACITY];
    int headset_count;
    int next_headset;
    FakeFile files[FILE_CAPACITY];
    int file_count;
    bool fail_music_writes;
    const char *fail_file_writes;
    const char *fail_internal_writes;
    const char *fail_file_opens;
    const char *fail_file_reads;
    InternalFile internal[INTERNAL_CAPACITY];
    FakeAudio audio;
    WrittenFile written[FILE_CAPACITY];
    int write_count;
    uint32_t clock_mhz;
    int stall_test_count;
    int watchdog_test_count;
    bool scratch_refused;
    int listing_borrows;
    int keep_alive_count;
    uint32_t idle_us;
    int audio_stats_resets;
    int usb_reconnects;
    HalProbe probe_answer;
    uint8_t power_levels;
    uint32_t power_samples;
    bool supply_held_off;
    int probe_count;
    FakeProbeCall probe_calls[PROBE_CALL_CAPACITY];
    HalRtcRead rtc_answer;
    uint32_t rtc_written_s;
    HalWallTime wall;
    uint32_t wall_set_ms;
    uint32_t wall_reads;
    int rtc_reads;
    HalUsbSense usb_sense;
    uint32_t saved_reports[FILE_CAPACITY];
    int saved_report_count;
    HalAudioState audio_state_at_report;
    uint32_t script_mark;
    char screenshot_path[PATH_CAPACITY];
    uint32_t screenshot_size;
    HalProfileState profile_state;
    int profile_resets;
    HalAudioStats audio_stats;
    bool restart_pending;
    uint32_t loaded_module;
    uint32_t broken_modules;
    int module_loads;
    int module_refusals;
    HalReserveResult reserve_result;
    const char *reserved_paths[FILE_CAPACITY];
    uint32_t reserved_sizes[FILE_CAPACITY];
    int reserve_count;
    int scratch_borrows;
    bool empty_card;
    int dir_opens;
    int presents_at_dir_open;
    int library_writes;
    int covers_writes;
    const char *store_used[STORE_SLOTS];
    int scratch_outstanding;
    int presents_at_power_on;
    bool refuse_clock_switch;
    int test_tone_count;
    int loop_bench_count;
    int core1_test_count;
    int core1_accel_count;
    bool dma_tone_running;
    uint32_t dma_tone_rate;
    int audio_service_count;
    HalBluetoothStatus bluetooth;
    bool bluetooth_powered;
    int bluetooth_scan_count;
    int bluetooth_stop_count;
    int bluetooth_service_count;
    int bluetooth_connect_count;
    HalBluetoothDevice bluetooth_connected_to;
    int bluetooth_disconnect_count;
    HalUsbMode usb_mode;
    HalUsbAnswer usb_answer;
    HalUsbMode requested_usb_mode;
    int usb_mode_requests;
    uint8_t dac_volume;
} FakeHal;

static FakeHal fake;
static HalCardInfo card_info;
static bool card_format_result;
static int card_format_calls;

void fake_hal_set_card(HalCardInfo info, bool result)
{
    card_info = info;
    card_format_result = result;
    card_format_calls = 0;
}

int fake_hal_card_formats(void)
{
    return card_format_calls;
}

HalCardInfo hal_card_info(void)
{
    HalCardInfo info = card_info;
    info.usb_busy |= fake.usb_sense.session || fake.usb_sense.connecting;
    return info;
}

bool hal_card_format(HalCardInfo expected, HalCardProgress progress, void *context)
{
    if (!card_info.present || expected.sectors != card_info.sectors ||
        expected.generation != card_info.generation) {
        return false;
    }
    card_format_calls++;
    if (progress != NULL && !progress(context, 0, 100)) {
        return false;
    }
    if (card_format_result) {
        fake_hal_replace_music_volume();
        fake.empty_card = true;
    }
    if (progress != NULL) {
        progress(context, 100, 100);
    }
    return card_format_result;
}

void fake_hal_replace_music_volume(void)
{
    fake.file_count = 0;
    memset(fake.written, 0, sizeof fake.written);
}

enum {
    EQ_FILE_CAPACITY = 4,
    EQ_NAME_CAPACITY = 32,
    EQ_LISTING_HANDLE = 77
};

static char eq_names[EQ_FILE_CAPACITY][EQ_NAME_CAPACITY];
static char eq_paths[EQ_FILE_CAPACITY][EQ_NAME_CAPACITY + 8];
static int eq_count;
static int eq_next;

enum {
    LANGUAGE_PACK_CAPACITY = 4,
    LANGUAGE_FILE_CAPACITY = 12,
    LANGUAGE_NAME_CAPACITY = 32,
    LANGUAGE_LISTING_HANDLE = 78
};

typedef struct LanguagePack {
    const uint8_t *bytes;
    uint32_t size;
} LanguagePack;

static const char LANGUAGE_FOLDER_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/lang";
static LanguagePack language_packs[HAL_LANGUAGE_COPIES][LANGUAGE_PACK_CAPACITY];
static char language_names[LANGUAGE_FILE_CAPACITY][LANGUAGE_NAME_CAPACITY];
static char language_paths[LANGUAGE_FILE_CAPACITY][LANGUAGE_NAME_CAPACITY + 24];
static int language_file_count;
static int language_next;
static int language_reads;

void fake_hal_reset(void)
{
    fake = (FakeHal){.reserve_result = HAL_RESERVE_CREATED};
    fake_hal_set_card((HalCardInfo){0}, false);
    fake.audio.headphones = headphone_audio_defaults();
    eq_count = 0;
    language_file_count = 0;
    language_reads = 0;
    memset(language_packs, 0, sizeof language_packs);
    fake.audio.jack = HAL_AUDIO_JACK_SINGLE_ENDED;
    fake.audio.status.idle = true;
}

void fake_hal_set_version(const char *version)
{
    fake.version = version;
}

const char *hal_system_version(void)
{
    return fake.version != NULL ? fake.version : "test";
}

void fake_hal_refuse_clock_switch(void)
{
    fake.refuse_clock_switch = true;
}

uint32_t hal_system_clock_mhz(void)
{
    return fake.clock_mhz != 0 ? fake.clock_mhz : 24;
}

bool hal_system_set_clock_mhz(uint32_t mhz)
{
    if (fake.refuse_clock_switch) {
        return false;
    }
    fake.clock_mhz = mhz;
    return true;
}

void hal_system_coast(bool coast)
{
    (void)coast;
}

static void copy_path(char *target, const char *source);

void fake_hal_add_file(const char *path, const uint8_t *bytes, uint32_t size)
{
    FakeFile *file = &fake.files[fake.file_count++];
    *file = (FakeFile){.bytes = bytes, .size = size, .stored = size};
    copy_path(file->path, path);
}

void fake_hal_set_milliseconds(uint32_t now_ms)
{
    fake.now_ms = now_ms;
}

void fake_hal_set_microseconds_per_read(uint32_t step_us)
{
    fake.microseconds_per_read = step_us;
}

void fake_hal_queue_transition(Key key, bool pressed, uint32_t time_ms)
{
    if (fake.transition_count == TRANSITION_CAPACITY) {
        return;
    }
    fake.transitions[fake.transition_count++] =
        (HalKeyTransition){.key = key, .pressed = pressed, .time_ms = time_ms};
}

static int panel_width(void)
{
    return fake.rotation == HAL_DISPLAY_LANDSCAPE ? HAL_DISPLAY_LONG_SIDE : HAL_DISPLAY_SHORT_SIDE;
}

uint16_t fake_hal_pixel(int x, int y)
{
    return fake.panel[y * panel_width() + x];
}

HalDisplayRotation fake_hal_display_rotation(void)
{
    return fake.rotation;
}

void hal_display_set_rotation(HalDisplayRotation rotation)
{
    fake.rotation = rotation;
    memset(fake.panel, 0, sizeof fake.panel);
}

long fake_hal_pixels_written(void)
{
    return fake.pixels_written;
}

int fake_hal_present_count(void)
{
    return fake.present_count;
}

void hal_display_write(int x, int y, int width, int height, const uint16_t *pixels)
{
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            fake.panel[(y + row) * panel_width() + x + column] = pixels[row * width + column];
        }
    }
    fake.pixels_written += (long)width * height;
    FakeDisplayWrites *writes = &fake.writes;
    FakeArea area = {x, y, width, height};
    if (writes->count == 0) {
        writes->first = area;
        writes->narrowest = width;
    }
    writes->narrowest = width < writes->narrowest ? width : writes->narrowest;
    writes->same &=
        writes->count == 0 || (writes->first.x == x && writes->first.y == y &&
                               writes->first.width == width && writes->first.height == height);
    writes->count++;
}

void fake_hal_forget_display_writes(void)
{
    fake.writes = (FakeDisplayWrites){.same = true};
}

FakeDisplayWrites fake_hal_display_writes(void)
{
    return fake.writes;
}

void hal_display_present(void)
{
    fake.present_count++;
}

void hal_display_set_power(bool on)
{
    if (on && !fake.display_on) {
        fake.presents_at_power_on = fake.present_count;
    }
    fake.display_on = on;
}

int fake_hal_presents_at_power_on(void)
{
    return fake.presents_at_power_on;
}

void hal_display_set_brightness(uint8_t level)
{
    fake.brightness = level;
}

bool fake_hal_display_on(void)
{
    return fake.display_on;
}

uint8_t fake_hal_display_brightness(void)
{
    return fake.brightness;
}

void fake_hal_set_battery(HalBattery battery)
{
    fake.battery = battery;
}

HalBattery hal_power_battery(void)
{
    return fake.battery;
}

void hal_power_off(void)
{
    uint32_t reason = debug_loop_take_shutdown();
    if (reason != DEBUG_REPORT_NONE) {
        hal_system_save_report(reason);
    }
    fake.power_off_count++;
    fake.audio_state_at_power_off = fake.audio.status.state;
}

int fake_hal_audio_state_at_power_off(void)
{
    return (int)fake.audio_state_at_power_off;
}

int fake_hal_power_off_count(void)
{
    return fake.power_off_count;
}

void fake_hal_queue_headset_command(HalHeadsetCommand command)
{
    if (fake.headset_count == TRANSITION_CAPACITY) {
        return;
    }
    fake.headset_commands[fake.headset_count++] = command;
}

bool hal_input_next_headset_command(HalHeadsetCommand *command)
{
    if (fake.next_headset == fake.headset_count) {
        fake.next_headset = 0;
        fake.headset_count = 0;
        return false;
    }
    *command = fake.headset_commands[fake.next_headset++];
    return true;
}

HalHeadsetReading hal_input_headset_reading(void)
{
    return (HalHeadsetReading){.known = true, .level = 812, .button = HAL_HEADSET_BUTTON_NONE};
}

bool hal_input_waiting(void)
{
    return fake.next_transition != fake.transition_count;
}

bool hal_input_next_transition(HalKeyTransition *transition)
{
    if (fake.next_transition == fake.transition_count) {
        fake.next_transition = 0;
        fake.transition_count = 0;
        return false;
    }
    *transition = fake.transitions[fake.next_transition++];
    return true;
}

void fake_hal_set_wall(uint32_t utc_s, int16_t offset_minutes)
{
    fake.wall = (HalWallTime){.utc_s = utc_s,
                              .offset_minutes = offset_minutes,
                              .source = utc_s != 0 ? HAL_WALL_COMPUTER : HAL_WALL_UNKNOWN};
    fake.wall_set_ms = hal_clock_milliseconds();
}

HalWallTime hal_clock_wall(void)
{
    fake.wall_reads++;
    HalWallTime time = fake.wall;
    if (time.source != HAL_WALL_UNKNOWN) {
        time.utc_s += (hal_clock_milliseconds() - fake.wall_set_ms) / 1000;
    }
    return time;
}

uint32_t fake_hal_wall_reads(void)
{
    return fake.wall_reads;
}

uint32_t hal_clock_milliseconds(void)
{
    return fake.now_ms;
}

uint32_t fake_hal_microseconds(void)
{
    return fake.now_us;
}

uint32_t hal_clock_microseconds(void)
{
    fake.now_us += fake.microseconds_per_read;
    return fake.now_us;
}

bool hal_file_open(HalFile *file, const char *path)
{
    if (fake.fail_file_opens != NULL && strcmp(fake.fail_file_opens, path) == 0) {
        *file = (HalFile){.handle = -1, .size = 0};
        return false;
    }
    for (int index = 0; index < fake.file_count; index++) {
        if (strcmp(fake.files[index].path, path) == 0) {
            *file = (HalFile){.handle = index, .size = fake.files[index].size};
            return true;
        }
    }
    *file = (HalFile){.handle = -1, .size = 0};
    return false;
}

bool hal_file_read(HalFile file, uint32_t offset, void *buffer, uint32_t length)
{
    if (file.handle < 0 || file.handle >= fake.file_count) {
        return false;
    }
    const FakeFile *source = &fake.files[file.handle];
    if (fake.fail_file_reads != NULL && strcmp(fake.fail_file_reads, source->path) == 0) {
        return false;
    }
    if (offset > source->size || length > source->size - offset) {
        return false;
    }
    uint32_t stored = source->bytes == NULL ? 0 : source->stored;
    for (uint32_t index = 0; index < length; index++) {
        uint32_t position = offset + index;
        ((uint8_t *)buffer)[index] = position < stored ? source->bytes[position] : 0;
    }
    return true;
}

void hal_file_close(HalFile *file)
{
    file->handle = -1;
}

static void copy_path(char *target, const char *source)
{
    strncpy(target, source, PATH_CAPACITY - 1);
    target[PATH_CAPACITY - 1] = '\0';
}

const char *fake_hal_audio_playing_path(void)
{
    return fake.audio.playing_path;
}

uint32_t fake_hal_audio_play_start(void)
{
    return fake.audio.play_start_ms;
}

uint32_t fake_hal_audio_play_end(void)
{
    return fake.audio.play_end_ms;
}

uint32_t fake_hal_audio_queued_start(void)
{
    return fake.audio.queued_start_ms;
}

uint32_t fake_hal_audio_queued_end(void)
{
    return fake.audio.queued_end_ms;
}

int fake_hal_audio_release_count(void)
{
    return fake.audio.release_count;
}

int fake_hal_audio_play_count(void)
{
    return fake.audio.play_count;
}

const char *fake_hal_audio_queued_path(void)
{
    return fake.audio.has_queued ? fake.audio.queued_path : NULL;
}

void fake_hal_audio_finish_track(void)
{
    if (!fake.audio.has_queued) {
        fake.audio.status.state = HAL_AUDIO_STOPPED;
        return;
    }
    fake.audio.status.token = fake.audio.queued_token;
    fake.audio.replay_gain = fake.audio.queued_replay_gain;
    fake.audio.status.position_ms = fake.audio.queued_start_ms;
    fake.audio.play_end_ms = fake.audio.queued_end_ms;
    copy_path(fake.audio.playing_path, fake.audio.queued_path);
    fake.audio.has_queued = false;
}

void fake_hal_audio_refuse_plays(int count)
{
    fake.audio.refused_plays = count;
}

void fake_hal_audio_fail_track(void)
{
    fake.audio.status.state = HAL_AUDIO_STOPPED;
    fake.audio.status.failed = true;
}

void fake_hal_audio_set_position(uint32_t position_ms)
{
    fake.audio.status.position_ms = position_ms;
}

void fake_hal_audio_set_format(HalAudioFormat format)
{
    fake.audio.status.format = format;
}

void fake_hal_audio_set_idle(bool idle)
{
    fake.audio.status.idle = idle;
}

void fake_hal_audio_set_token(uint32_t token)
{
    fake.audio.status.token = token;
}

int fake_hal_audio_seek_count(void)
{
    return fake.audio.seek_count;
}

uint32_t fake_hal_audio_last_seek(void)
{
    return fake.audio.last_seek_ms;
}

uint8_t fake_hal_audio_volume(void)
{
    return fake.audio.volume;
}

int fake_hal_audio_state(void)
{
    return (int)fake.audio.status.state;
}

int fake_hal_write_count(void)
{
    return fake.write_count;
}

bool hal_audio_play(const HalAudioTrack *track, uint32_t offset_ms, const HalAudioTrack *next)
{
    uint32_t start_ms = track->start_ms + offset_ms;
    uint32_t token = track->token;
    fake.audio.play_count++;
    if (fake.audio.refused_plays > 0) {
        fake.audio.refused_plays--;
        fake.audio.status = (HalAudioStatus){
            .state = HAL_AUDIO_STOPPED, .token = token, .failed = true, .idle = false};
        return false;
    }
    copy_path(fake.audio.playing_path, track->path);
    fake.audio.play_start_ms = start_ms;
    fake.audio.play_end_ms = track->end_ms;
    fake.audio.replay_gain = track->replay_gain != 0 ? track->replay_gain : 65536;
    fake.audio.play_fade_ms = track->fade_ms;
    fake.audio.status = (HalAudioStatus){.state = HAL_AUDIO_PLAYING,
                                         .token = token,
                                         .position_ms = start_ms,
                                         .failed = false,
                                         .idle = false};
    hal_audio_queue_next(next);
    return true;
}

void hal_audio_queue_next(const HalAudioTrack *track)
{
    fake.audio.has_queued = track != NULL;
    if (track != NULL) {
        copy_path(fake.audio.queued_path, track->path);
        fake.audio.queued_token = track->token;
        fake.audio.queued_start_ms = track->start_ms;
        fake.audio.queued_end_ms = track->end_ms;
        fake.audio.queued_replay_gain = track->replay_gain != 0 ? track->replay_gain : 65536;
        fake.audio.queued_fade_ms = track->fade_ms;
    }
}

void hal_audio_pause(void)
{
    fake.audio.status.state = HAL_AUDIO_PAUSED;
}

void hal_audio_release(void)
{
    fake.audio.release_count++;
    fake.audio.status.state = HAL_AUDIO_STOPPED;
    fake.audio.status.idle = true;
}

void hal_audio_resume(void)
{
    fake.audio.status.state = HAL_AUDIO_PLAYING;
}

void hal_audio_seek(uint32_t position_ms)
{
    fake.audio.seek_count++;
    fake.audio.last_seek_ms = position_ms;
    fake.audio.status.position_ms = position_ms;
}

void hal_audio_set_volume(uint8_t level)
{
    fake.audio.volume = headphone_audio_limit(fake.audio.headphones, level);
}

void hal_audio_set_headphones(HeadphoneAudio profile)
{
    fake.audio.volume = headphone_audio_limit(profile, fake.audio.volume);
    fake.audio.headphones = profile;
}

HeadphoneAudio hal_audio_headphones(void)
{
    return fake.audio.headphones;
}

void hal_audio_set_gain(HalAudioGain gain)
{
    fake.audio.gain = gain;
}

void hal_audio_set_output(HalAudioOutput output)
{
    fake.audio.output = output;
}

void hal_audio_set_equalizer(const HalAudioEqualizer *equalizer)
{
    fake.audio.equalizer = *equalizer;
}

HalAudioEqualizer fake_hal_audio_equalizer(void)
{
    return fake.audio.equalizer;
}

int fake_hal_audio_gain(void)
{
    return (int)fake.audio.gain;
}

int fake_hal_audio_output(void)
{
    return (int)fake.audio.output;
}

int fake_hal_test_tone_count(void)
{
    return fake.test_tone_count;
}

HalToneResult hal_audio_test_tone(void)
{
    fake.test_tone_count++;
    return HAL_TONE_PLAYED;
}

int fake_hal_audio_service_count(void)
{
    return fake.audio_service_count;
}

void hal_audio_service(void)
{
    fake.audio_service_count++;
}

HalAudioStats hal_audio_stats(void)
{
    return fake.audio_stats;
}

void fake_hal_set_audio_stats(HalAudioStats stats)
{
    fake.audio_stats = stats;
}

void hal_audio_reset_stats(void)
{
    fake.audio_stats_resets++;
    fake.audio_stats = (HalAudioStats){.underruns = fake.audio_stats.underruns};
}

int fake_hal_audio_stats_reset_count(void)
{
    return fake.audio_stats_resets;
}

HalUsbSense hal_system_usb_sense(void)
{
    return fake.usb_sense;
}

void hal_system_keep_usb_on(bool kept_on)
{
    fake.usb_sense.kept_on = kept_on;
    fake.usb_sense.powered = kept_on;
}

void fake_hal_set_usb_cable(bool cable)
{
    fake.usb_sense.cable = cable;
}

void fake_hal_set_usb_session(bool session)
{
    fake.usb_sense.session = session;
}

void hal_system_usb_reconnect(void)
{
    fake.usb_reconnects++;
}

void hal_system_usb_connect(bool on)
{
    fake.usb_sense.connecting = on;
}

uint8_t hal_system_power_levels(void)
{
    return (uint8_t)(fake.power_levels | (fake.supply_held_off ? HAL_PROBE_SUPPLY_OFF : 0));
}

uint32_t hal_system_power_samples(void)
{
    return fake.power_samples;
}

void hal_system_hold_usb_supply_off(bool held)
{
    fake.supply_held_off = held;
}

void fake_hal_set_power_levels(uint8_t levels)
{
    fake.power_levels = levels;
}

void fake_hal_set_power_samples(uint32_t samples)
{
    fake.power_samples = samples;
}

bool fake_hal_supply_held_off(void)
{
    return fake.supply_held_off;
}

HalProbe hal_system_probe(HalProbeKind kind, uint32_t first, uint32_t second)
{
    if (fake.probe_count < PROBE_CALL_CAPACITY) {
        fake.probe_calls[fake.probe_count] = (FakeProbeCall){kind, first, second};
    }
    fake.probe_count++;
    return fake.probe_answer;
}

void fake_hal_set_probe_answer(HalProbe answer)
{
    fake.probe_answer = answer;
}

int fake_hal_probe_count(void)
{
    return fake.probe_count;
}

FakeProbeCall fake_hal_probe_call(int index)
{
    return fake.probe_calls[index];
}

HalRtcRead hal_system_rtc_read(void)
{
    fake.rtc_reads++;
    return fake.rtc_answer;
}

HalRtcRead hal_system_rtc_set(uint32_t utc_s)
{
    fake.rtc_reads++;
    fake.rtc_written_s = utc_s;
    return fake.rtc_answer;
}

uint32_t fake_hal_rtc_written(void)
{
    return fake.rtc_written_s;
}

void fake_hal_set_rtc_answer(HalRtcRead answer)
{
    fake.rtc_answer = answer;
}

int fake_hal_rtc_read_count(void)
{
    return fake.rtc_reads;
}

void hal_system_save_report(uint32_t reason)
{
    if (fake.saved_report_count < FILE_CAPACITY) {
        fake.saved_reports[fake.saved_report_count] = reason;
    }
    fake.saved_report_count++;
    fake.audio_state_at_report = fake.audio.status.state;
}

int fake_hal_saved_report_count(void)
{
    return fake.saved_report_count;
}

uint32_t fake_hal_saved_report(int index)
{
    return fake.saved_reports[index];
}

int fake_hal_usb_reconnect_count(void)
{
    return fake.usb_reconnects;
}

void hal_system_script_mark(uint32_t line)
{
    fake.script_mark = line;
}

uint32_t fake_hal_script_mark(void)
{
    return fake.script_mark;
}

HalToneDetail hal_audio_tone_detail(void)
{
    return (HalToneDetail){.stage = 0, .pending = 0};
}

HalBootClocks hal_system_boot_clocks(void)
{
    return (HalBootClocks){0};
}

void hal_system_request_usb_mode(HalUsbMode mode)
{
    fake.requested_usb_mode = mode;
    fake.usb_mode_requests++;
}

int fake_hal_usb_mode_requests(void)
{
    return fake.usb_mode_requests;
}

HalUsbMode fake_hal_requested_usb_mode(void)
{
    return fake.requested_usb_mode;
}

void hal_system_set_usb_mode(HalUsbMode mode)
{
    fake.usb_mode = mode;
}

void hal_system_set_usb_answer(HalUsbAnswer answer)
{
    fake.usb_answer = answer;
}

HalUsbAnswer fake_hal_usb_answer(void)
{
    return fake.usb_answer;
}

int fake_hal_usb_mode(void)
{
    return (int)fake.usb_mode;
}

void hal_system_set_dac_volume(uint8_t level)
{
    fake.dac_volume = level;
}

int fake_hal_dac_volume(void)
{
    return fake.dac_volume;
}

HalUsbLast hal_system_usb_last(void)
{
    return (HalUsbLast){.end = HAL_USB_END_NONE};
}

HalUsbCore hal_system_usb_core(void)
{
    return (HalUsbCore){.captured = false};
}

uint32_t fake_hal_dma_tone_rate(void)
{
    return fake.dma_tone_rate;
}

HalToneResult hal_audio_dma_tone_start(uint32_t sample_rate)
{
    fake.dma_tone_rate = sample_rate;
    fake.dma_tone_running = true;
    return HAL_TONE_PLAYED;
}

void hal_audio_dma_tone_stop(void)
{
    fake.dma_tone_running = false;
}

HalDmaToneStatus hal_audio_dma_tone_status(void)
{
    return (HalDmaToneStatus){.running = fake.dma_tone_running, .position = 0};
}

HalAudioRefusal hal_audio_refusal(void)
{
    return fake.audio.refusal;
}

void fake_hal_audio_set_refusal(HalAudioRefusal refusal)
{
    fake.audio.refusal = refusal;
}

HalAudioJack hal_audio_jack(void)
{
    return fake.audio.jack;
}

void fake_hal_audio_set_jack(HalAudioJack jack)
{
    fake.audio.jack = jack;
}

DacIdentity hal_audio_dac_identity(int chip)
{
    return fake.audio.dac_identity[chip != 0];
}

void fake_hal_audio_set_dac_identity(int chip, DacIdentity identity)
{
    fake.audio.dac_identity[chip != 0] = identity;
}

HalJackPins hal_audio_jack_pins(void)
{
    return (HalJackPins){.single_ended = {.high = false}, .balanced = {.high = true}};
}

HalAudioStatus hal_audio_status(void)
{
    return fake.audio.status;
}

static WrittenFile *written_slot(const char *path)
{
    for (int index = 0; index < FILE_CAPACITY; index++) {
        if (strcmp(fake.written[index].path, path) == 0 || fake.written[index].path[0] == '\0') {
            return &fake.written[index];
        }
    }
    return NULL;
}

static void register_written(const char *path, uint32_t length)
{
    WrittenFile *slot = written_slot(path);
    for (int index = 0; index < fake.file_count; index++) {
        if (strcmp(fake.files[index].path, path) == 0) {
            fake.files[index].size = length;
            fake.files[index].bytes =
                fake.files[index].bytes == NULL ? slot->bytes : fake.files[index].bytes;
            fake.files[index].stored =
                fake.files[index].bytes == slot->bytes ? length : fake.files[index].stored;
            return;
        }
    }
    fake_hal_add_file(slot->path, slot->bytes, length);
}

bool hal_file_write(const char *path, const void *bytes, uint32_t length)
{
    WrittenFile *slot = written_slot(path);
    if (fake.fail_music_writes ||
        (fake.fail_file_writes != NULL && strcmp(path, fake.fail_file_writes) == 0) ||
        slot == NULL || length > WRITTEN_CAPACITY) {
        return false;
    }
    copy_path(slot->path, path);
    memcpy(slot->bytes, bytes, length);
    register_written(path, length);
    fake.write_count++;
    return true;
}

const uint8_t *fake_hal_file_bytes(const char *path, uint32_t *size)
{
    for (int index = 0; index < fake.file_count; index++) {
        if (strcmp(fake.files[index].path, path) == 0) {
            *size = fake.files[index].size;
            return fake.files[index].bytes;
        }
    }
    *size = 0;
    return NULL;
}

int fake_hal_loop_bench_count(void)
{
    return fake.loop_bench_count;
}

void hal_system_stall_test(void)
{
    fake.stall_test_count++;
}

int fake_hal_stall_test_count(void)
{
    return fake.stall_test_count;
}

void hal_system_watchdog_test(void)
{
    fake.watchdog_test_count++;
}

int fake_hal_watchdog_test_count(void)
{
    return fake.watchdog_test_count;
}

void hal_system_keep_alive(void)
{
    fake.keep_alive_count++;
}

uint32_t hal_system_idle_microseconds(void)
{
    return fake.idle_us;
}

void fake_hal_set_idle_microseconds(uint32_t idle_us)
{
    fake.idle_us = idle_us;
}

HalProfileState hal_system_profile_state(void)
{
    return fake.profile_state;
}

void hal_system_set_profiling(bool on)
{
    if (fake.profile_state != HAL_PROFILE_FULL) {
        fake.profile_state = on ? HAL_PROFILE_ON : HAL_PROFILE_OFF;
    }
}

void hal_system_reset_profile(void)
{
    fake.profile_resets++;
    if (fake.profile_state == HAL_PROFILE_FULL) {
        fake.profile_state = HAL_PROFILE_ON;
    }
}

void fake_hal_fill_profile(void)
{
    fake.profile_state = HAL_PROFILE_FULL;
}

int fake_hal_profile_reset_count(void)
{
    return fake.profile_resets;
}

int fake_hal_keep_alive_count(void)
{
    return fake.keep_alive_count;
}

HalLoopCycles hal_system_loop_cycles(void)
{
    fake.loop_bench_count++;
    return (HalLoopCycles){.register_centi = 300, .memory_centi = 700};
}

int fake_hal_core1_test_count(void)
{
    return fake.core1_test_count;
}

HalCore1Test hal_system_core1_test(void)
{
    fake.core1_test_count++;
    return (HalCore1Test){.status = HAL_CORE1_RAN, .mhz = 100};
}

int fake_hal_core1_accel_count(void)
{
    return fake.core1_accel_count;
}

HalCore1Accel hal_system_core1_accel_test(void)
{
    fake.core1_accel_count++;
    return (HalCore1Accel){.status = HAL_CORE1_UNAVAILABLE};
}

HalPowerHold hal_power_boot_hold(void)
{
    return (HalPowerHold){.function = 0, .output = true, .high = true};
}

static bool usb_start;

void fake_hal_set_usb_start(bool started)
{
    usb_start = started;
}

bool hal_power_cable(void)
{
    return false;
}

bool hal_power_take_usb_start(void)
{
    bool started = usb_start;
    usb_start = false;
    return started;
}

HalStartInfo hal_system_start_info(void)
{
    return (HalStartInfo){.warm = false, .starts = 1};
}

HalBluetoothStatus *fake_hal_bluetooth(void)
{
    return &fake.bluetooth;
}

bool fake_hal_bluetooth_powered(void)
{
    return fake.bluetooth_powered;
}

int fake_hal_bluetooth_scan_count(void)
{
    return fake.bluetooth_scan_count;
}

int fake_hal_bluetooth_service_count(void)
{
    return fake.bluetooth_service_count;
}

void hal_bluetooth_set_power(bool on)
{
    HalBluetoothStatus kept = fake.bluetooth;
    fake.bluetooth_powered = on;
    fake.bluetooth = (HalBluetoothStatus){.state = on ? HAL_BLUETOOTH_STARTING : HAL_BLUETOOTH_OFF,
                                          .device_count = kept.device_count,
                                          .link = kept.link};
    memcpy(fake.bluetooth.devices, kept.devices, sizeof kept.devices);
}

void hal_bluetooth_stop_scan(void)
{
    fake.bluetooth_stop_count++;
    if (fake.bluetooth.state == HAL_BLUETOOTH_SCANNING) {
        fake.bluetooth.state = HAL_BLUETOOTH_READY;
    }
}

int fake_hal_bluetooth_stop_count(void)
{
    return fake.bluetooth_stop_count;
}

void hal_bluetooth_scan(void)
{
    fake.bluetooth_scan_count++;
    if (fake.bluetooth.state == HAL_BLUETOOTH_READY) {
        fake.bluetooth.state = HAL_BLUETOOTH_SCANNING;
    }
}

int fake_hal_bluetooth_connect_count(void)
{
    return fake.bluetooth_connect_count;
}

const HalBluetoothDevice *fake_hal_bluetooth_connect_target(void)
{
    return &fake.bluetooth_connected_to;
}

int fake_hal_bluetooth_disconnect_count(void)
{
    return fake.bluetooth_disconnect_count;
}

void hal_bluetooth_connect(const HalBluetoothDevice *device)
{
    fake.bluetooth_connect_count++;
    fake.bluetooth_connected_to = *device;
}

void hal_bluetooth_disconnect(void)
{
    fake.bluetooth_disconnect_count++;
}

void hal_bluetooth_set_stream(bool on)
{
    fake.bluetooth.streaming = on;
}

void hal_bluetooth_service(void)
{
    fake.bluetooth_service_count++;
}

const HalBluetoothStatus *hal_bluetooth_status(void)
{
    return &fake.bluetooth;
}

void fake_hal_add_eq_file(const char *name, const char *text)
{
    snprintf(eq_names[eq_count], EQ_NAME_CAPACITY, "%s", name);
    snprintf(eq_paths[eq_count], sizeof eq_paths[0], "/EQ/%s", name);
    fake_hal_add_file(eq_paths[eq_count], (const uint8_t *)text, (uint32_t)strlen(text));
    eq_count++;
}

void fake_hal_clear_eq_files(void)
{
    eq_count = 0;
}

void fake_hal_add_language_pack(int copy, const uint8_t *bytes, uint32_t size)
{
    for (int index = 0; index < LANGUAGE_PACK_CAPACITY; index++) {
        if (language_packs[copy][index].bytes == NULL) {
            language_packs[copy][index] = (LanguagePack){bytes, size};
            return;
        }
    }
}

void fake_hal_add_language_file(const char *name, const uint8_t *bytes, uint32_t size)
{
    int index = language_file_count++;
    snprintf(language_names[index], LANGUAGE_NAME_CAPACITY, "%s", name);
    snprintf(language_paths[index], sizeof language_paths[0], "%s/%s", LANGUAGE_FOLDER_PATH, name);
    fake_hal_add_file(language_paths[index], bytes, size);
}

int fake_hal_language_read_count(void)
{
    return language_reads;
}

uint8_t *hal_language_memory(uint32_t *capacity)
{
    static uint8_t memory[I18N_PACK_CAPACITY];
    *capacity = sizeof memory;
    return memory;
}

bool hal_language_read(HalLanguagePack pack, uint32_t offset, void *buffer, uint32_t length)
{
    language_reads++;
    if (fake.restart_pending || pack.copy >= HAL_LANGUAGE_COPIES ||
        pack.index >= LANGUAGE_PACK_CAPACITY) {
        return false;
    }
    const LanguagePack *source = &language_packs[pack.copy][pack.index];
    if (source->bytes == NULL || offset > source->size || length > source->size - offset) {
        return false;
    }
    memcpy(buffer, source->bytes + offset, length);
    return true;
}

bool hal_dir_open(HalDir *dir, const char *path)
{
    if (strcmp(path, "/EQ") == 0) {
        dir->handle = EQ_LISTING_HANDLE;
        eq_next = 0;
        return eq_count > 0;
    }
    if (strcmp(path, LANGUAGE_FOLDER_PATH) == 0) {
        dir->handle = LANGUAGE_LISTING_HANDLE;
        language_next = 0;
        return language_file_count > 0;
    }
    fake.dir_opens++;
    fake.presents_at_dir_open = fake.present_count;
    dir->handle = fake.empty_card ? 0 : -1;
    return fake.empty_card;
}

void fake_hal_set_empty_card(bool empty)
{
    fake.empty_card = empty;
}

int fake_hal_dir_open_count(void)
{
    return fake.dir_opens;
}

int fake_hal_presents_at_last_dir_open(void)
{
    return fake.presents_at_dir_open;
}

int fake_hal_covers_writes(void)
{
    return fake.covers_writes;
}

int fake_hal_library_writes(void)
{
    return fake.library_writes;
}

bool hal_dir_next(HalDir *dir, HalDirEntry *entry)
{
    if (dir->handle == LANGUAGE_LISTING_HANDLE && language_next < language_file_count) {
        *entry = (HalDirEntry){.directory = false, .hidden = false};
        snprintf(entry->name, sizeof entry->name, "%s", language_names[language_next++]);
        return true;
    }
    if (dir->handle != EQ_LISTING_HANDLE || eq_next >= eq_count) {
        return false;
    }
    *entry = (HalDirEntry){.directory = false, .hidden = false};
    snprintf(entry->name, sizeof entry->name, "%s", eq_names[eq_next++]);
    return true;
}

void hal_dir_close(HalDir *dir)
{
    dir->handle = -1;
}

void *hal_system_borrow_scratch(uint32_t *size)
{
    static uint8_t scratch[384 * 1024];
    fake.scratch_borrows++;
    if (fake.scratch_refused) {
        *size = 0;
        return NULL;
    }
    fake.scratch_outstanding++;
    *size = sizeof scratch;
    return scratch;
}

void hal_system_return_scratch(void)
{
    if (!fake.scratch_refused) {
        fake.scratch_outstanding--;
    }
}

void *hal_system_borrow_listing(uint32_t *size)
{
    static uint8_t listing[96 * 1024];
    fake.listing_borrows++;
    *size = sizeof listing;
    return listing;
}

int fake_hal_listing_borrow_count(void)
{
    return fake.listing_borrows;
}

void fake_hal_refuse_scratch(bool refused)
{
    fake.scratch_refused = refused;
}

int fake_hal_scratch_borrow_count(void)
{
    return fake.scratch_borrows;
}

int fake_hal_scratch_outstanding(void)
{
    return fake.scratch_outstanding;
}

static FakeFile *fake_file(const char *path)
{
    for (int index = 0; index < fake.file_count; index++) {
        if (strcmp(fake.files[index].path, path) == 0) {
            return &fake.files[index];
        }
    }
    return NULL;
}

static uint8_t *large_store_for(FakeFile *file, LargeStore *large)
{
    if (file->bytes != NULL && file->bytes == large->bytes) {
        return large->bytes;
    }
    if (file->size > large->size) {
        free(large->bytes);
        large->bytes = malloc(file->size);
        large->size = file->size;
    }
    if (large->bytes == NULL) {
        large->size = 0;
        return NULL;
    }
    memset(large->bytes, 0, file->size);
    file->bytes = large->bytes;
    file->stored = file->size;
    return large->bytes;
}

static uint8_t *store_for(FakeFile *file)
{
    for (int slot = 0; slot < STORE_SLOTS; slot++) {
        if (file->bytes == stores[slot]) {
            return stores[slot];
        }
    }
    if (file->size > STORE_CAPACITY &&
        strcmp(file->path, "/" FIRMWARE_DATA_DIRECTORY "/build.tmp") == 0) {
        return large_store_for(file, &large_stores[0]);
    }
    if (file->size > STORE_CAPACITY && strcmp(file->path, TRACK_KEYS_PATH) == 0) {
        return large_store_for(file, &large_stores[1]);
    }
    for (int slot = 0; slot < STORE_SLOTS; slot++) {
        if (fake.store_used[slot] == NULL) {
            fake.store_used[slot] = file->path;
            uint32_t kept = file->bytes == NULL ? 0 : file->stored;
            kept = kept < STORE_CAPACITY ? kept : STORE_CAPACITY;
            memset(stores[slot], 0, STORE_CAPACITY);
            if (kept > 0) {
                memcpy(stores[slot], file->bytes, kept);
            }
            file->bytes = stores[slot];
            file->stored = STORE_CAPACITY < file->size ? STORE_CAPACITY : file->size;
            return stores[slot];
        }
    }
    return NULL;
}

bool hal_file_write_at(const char *path, uint32_t offset, const void *bytes, uint32_t length)
{
    FakeFile *file = fake_file(path);
    if (fake.fail_music_writes ||
        (fake.fail_file_writes != NULL && strcmp(path, fake.fail_file_writes) == 0) ||
        file == NULL || offset > file->size || length > file->size - offset) {
        return false;
    }
    uint8_t *store = store_for(file);
    if (store != NULL && offset + length <= file->stored) {
        memcpy(store + offset, bytes, length);
    }
    fake.library_writes += strcmp(path, "/" FIRMWARE_DATA_DIRECTORY "/library.db") == 0;
    fake.covers_writes += strcmp(path, "/" FIRMWARE_DATA_DIRECTORY "/covers.bin") == 0;
    return true;
}

bool hal_file_write_open(HalFile file, uint32_t offset, const void *bytes, uint32_t length)
{
    if (file.handle < 0 || file.handle >= fake.file_count) {
        return false;
    }
    return hal_file_write_at(fake.files[file.handle].path, offset, bytes, length);
}

HalHighRamTest hal_system_high_ram_test(void)
{
    return (HalHighRamTest){.status = HAL_HIGH_RAM_UNAVAILABLE};
}

void fake_hal_set_reserve_result(HalReserveResult result)
{
    fake.reserve_result = result;
}

HalVolume hal_file_volume(void)
{
    return HAL_VOLUME_CARD;
}

HalReserveResult hal_file_reserve(const char *path, uint32_t size)
{
    if (fake.reserve_count < FILE_CAPACITY) {
        fake.reserved_paths[fake.reserve_count] = path;
        fake.reserved_sizes[fake.reserve_count] = size;
    }
    fake.reserve_count++;
    HalFile existing;
    if (hal_file_open(&existing, path)) {
        return HAL_RESERVE_EXISTS;
    }
    if (fake.reserve_result == HAL_RESERVE_CREATED) {
        fake_hal_add_file(path, NULL, size);
    }
    return fake.reserve_result;
}

int fake_hal_reserve_count(void)
{
    return fake.reserve_count;
}

uint32_t fake_hal_reserved_size(const char *path)
{
    for (int index = 0; index < fake.reserve_count && index < FILE_CAPACITY; index++) {
        if (strcmp(fake.reserved_paths[index], path) == 0) {
            return fake.reserved_sizes[index];
        }
    }
    return 0;
}

void fake_hal_set_restart_pending(bool pending)
{
    fake.restart_pending = pending;
}

bool hal_system_restart_pending(void)
{
    return fake.restart_pending;
}

const void *hal_system_load_rescan_code(void)
{
    return &RESCAN_MODULE_ENTRY;
}

const void *hal_system_load_module(uint32_t module)
{
    if (module != fake.loaded_module) {
        fake.loaded_module = 0;
        if (fake.restart_pending || (fake.broken_modules & 1u << module) != 0) {
            fake.module_refusals++;
            return NULL;
        }
        fake.module_loads++;
        fake.loaded_module = module;
    }
    return app_linked_module(module);
}

const void *hal_system_load_debug_code(void)
{
    if ((fake.broken_modules & 1u << EXTENSION_MODULE_NUMBER) != 0) {
        fake.module_refusals++;
        return NULL;
    }
    return app_linked_debug_code();
}

void fake_hal_break_module(uint32_t module, bool broken)
{
    uint32_t bit = 1u << module;
    fake.broken_modules = broken ? fake.broken_modules | bit : fake.broken_modules & ~bit;
}

void fake_hal_evict_module(void)
{
    fake.loaded_module = 0;
}

uint32_t fake_hal_loaded_module(void)
{
    return fake.loaded_module;
}

int fake_hal_module_loads(void)
{
    return fake.module_loads;
}

int fake_hal_module_refusals(void)
{
    return fake.module_refusals;
}

static InternalFile *internal_file(const char *path)
{
    for (int index = 0; index < INTERNAL_CAPACITY; index++) {
        if (fake.internal[index].path[0] != '\0' && strcmp(fake.internal[index].path, path) == 0) {
            return &fake.internal[index];
        }
    }
    return NULL;
}

static InternalFile *internal_slot(const char *path)
{
    InternalFile *existing = internal_file(path);
    for (int index = 0; existing == NULL && index < INTERNAL_CAPACITY; index++) {
        if (fake.internal[index].path[0] == '\0') {
            existing = &fake.internal[index];
            copy_path(existing->path, path);
        }
    }
    return existing;
}

bool hal_internal_file_read(const char *path, void *buffer, uint32_t length)
{
    InternalFile *file = internal_file(path);
    if (file == NULL || file->size < length || length > WRITTEN_CAPACITY) {
        return false;
    }
    memcpy(buffer, file->bytes, length);
    return true;
}

bool hal_internal_file_write(const char *path, const void *bytes, uint32_t length)
{
    InternalFile *file = internal_slot(path);
    if (file == NULL || length > WRITTEN_CAPACITY ||
        (fake.fail_file_writes != NULL && strcmp(path, fake.fail_file_writes) == 0) ||
        (fake.fail_internal_writes != NULL && strcmp(path, fake.fail_internal_writes) == 0)) {
        return false;
    }
    memcpy(file->bytes, bytes, length);
    file->size = length;
    fake.write_count++;
    return true;
}

static uint8_t screenshot[SCREENSHOT_CAPACITY];

static bool is_screenshot(const char *path)
{
    size_t length = strlen(path);
    return length > 4 && strcmp(path + length - 4, ".565") == 0;
}

static HalReserveResult reserve_screenshot(const char *path, uint32_t size)
{
    bool existed = strcmp(fake.screenshot_path, path) == 0;
    bool refused =
        fake.reserve_result == HAL_RESERVE_NO_SPACE || fake.reserve_result == HAL_RESERVE_FAILED;
    if (size > sizeof screenshot || refused) {
        return refused ? fake.reserve_result : HAL_RESERVE_FAILED;
    }
    snprintf(fake.screenshot_path, sizeof fake.screenshot_path, "%s", path);
    fake.screenshot_size = size;
    return existed ? HAL_RESERVE_EXISTS : HAL_RESERVE_CREATED;
}

bool hal_internal_file_write_at(const char *path, uint32_t offset, const void *bytes,
                                uint32_t length)
{
    InternalFile *file = internal_file(path);
    if (fake.fail_internal_writes != NULL && strcmp(path, fake.fail_internal_writes) == 0) {
        return false;
    }
    if (file != NULL && offset <= file->size && length <= file->size - offset &&
        offset + length <= sizeof file->bytes) {
        memcpy(file->bytes + offset, bytes, length);
        fake.write_count++;
        return true;
    }
    if (strcmp(fake.screenshot_path, path) != 0 || offset > fake.screenshot_size ||
        length > fake.screenshot_size - offset) {
        return false;
    }
    memcpy(screenshot + offset, bytes, length);
    return true;
}

const uint8_t *fake_hal_screenshot(const char **path, uint32_t *size)
{
    *path = fake.screenshot_path;
    *size = fake.screenshot_size;
    return screenshot;
}

HalReserveResult hal_internal_file_reserve(const char *path, uint32_t size)
{
    if (is_screenshot(path)) {
        return reserve_screenshot(path, size);
    }
    if (fake.reserve_count < FILE_CAPACITY) {
        fake.reserved_paths[fake.reserve_count] = path;
        fake.reserved_sizes[fake.reserve_count] = size;
    }
    fake.reserve_count++;
    if (internal_file(path) != NULL) {
        return HAL_RESERVE_EXISTS;
    }
    InternalFile *file = internal_slot(path);
    if (fake.reserve_result == HAL_RESERVE_CREATED && file != NULL) {
        memset(file->bytes, 0, sizeof file->bytes);
        file->size = size;
    } else if (file != NULL) {
        file->path[0] = '\0';
    }
    return fake.reserve_result;
}

const uint8_t *fake_hal_internal_file_bytes(const char *path, uint32_t *size)
{
    InternalFile *file = internal_file(path);
    *size = file == NULL ? 0 : file->size;
    return file == NULL ? NULL : file->bytes;
}

bool hal_file_remove(const char *path)
{
    FakeFile *file = fake_file(path);
    if (file == NULL || fake.fail_music_writes ||
        (fake.fail_file_writes != NULL && strcmp(path, fake.fail_file_writes) == 0)) {
        return false;
    }
    for (int slot = 0; slot < STORE_SLOTS; slot++) {
        if (file->bytes == stores[slot]) {
            fake.store_used[slot] = NULL;
        }
    }
    *file = fake.files[--fake.file_count];
    return true;
}

void fake_hal_fail_music_writes(bool fail)
{
    fake.fail_music_writes = fail;
}

void fake_hal_fail_file_writes(const char *path)
{
    fake.fail_file_writes = path;
}

void fake_hal_fail_file_opens(const char *path)
{
    fake.fail_file_opens = path;
}

void fake_hal_fail_file_reads(const char *path)
{
    fake.fail_file_reads = path;
}

void hal_audio_set_replay_gain(uint32_t token, uint32_t gain)
{
    if (fake.audio.status.token == token) {
        fake.audio.replay_gain = gain != 0 ? gain : 65536;
    }
    if (fake.audio.has_queued && fake.audio.queued_token == token) {
        fake.audio.queued_replay_gain = gain != 0 ? gain : 65536;
    }
}

uint32_t fake_hal_audio_replay_gain(void)
{
    return fake.audio.replay_gain;
}

uint32_t fake_hal_audio_queued_replay_gain(void)
{
    return fake.audio.queued_replay_gain;
}

uint32_t fake_hal_audio_queued_fade_ms(void)
{
    return fake.audio.has_queued ? fake.audio.queued_fade_ms : 0;
}

uint32_t fake_hal_audio_play_fade_ms(void)
{
    return fake.audio.play_fade_ms;
}

bool hal_clock_set_wall(HalWallTime time)
{
    if ((fake.broken_modules & 1u << EXTENSION_MODULE_NUMBER) != 0 ||
        hal_system_rtc_set(time.utc_s).status != HAL_RTC_OK) {
        return false;
    }
    fake.wall = time;
    fake.wall_set_ms = hal_clock_milliseconds();
    return true;
}

void fake_hal_fail_internal_writes(const char *path)
{
    fake.fail_internal_writes = path;
}
