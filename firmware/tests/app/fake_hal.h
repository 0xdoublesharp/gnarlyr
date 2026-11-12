#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/system.h"

void fake_hal_reset(void);
void fake_hal_set_version(const char *version);
void fake_hal_add_file(const char *path, const uint8_t *bytes, uint32_t size);
void fake_hal_replace_music_volume(void);
void fake_hal_add_eq_file(const char *name, const char *text);
void fake_hal_add_language_pack(int copy, const uint8_t *bytes, uint32_t size);
void fake_hal_add_language_file(const char *name, const uint8_t *bytes, uint32_t size);
int fake_hal_language_read_count(void);
void fake_hal_clear_eq_files(void);
const uint8_t *fake_hal_internal_file_bytes(const char *path, uint32_t *size);
void fake_hal_set_milliseconds(uint32_t now_ms);
void fake_hal_set_microseconds_per_read(uint32_t step_us);
uint32_t fake_hal_microseconds(void);
void fake_hal_refuse_clock_switch(void);
void fake_hal_audio_set_idle(bool idle);
int fake_hal_test_tone_count(void);
int fake_hal_stall_test_count(void);
int fake_hal_watchdog_test_count(void);
int fake_hal_keep_alive_count(void);
void fake_hal_set_audio_stats(HalAudioStats stats);
int fake_hal_audio_stats_reset_count(void);
int fake_hal_usb_reconnect_count(void);
void fake_hal_set_usb_cable(bool cable);
void fake_hal_set_usb_session(bool session);

typedef struct FakeProbeCall {
    HalProbeKind kind;
    uint32_t first;
    uint32_t second;
} FakeProbeCall;

void fake_hal_set_probe_answer(HalProbe answer);
int fake_hal_probe_count(void);
FakeProbeCall fake_hal_probe_call(int index);
void fake_hal_set_rtc_answer(HalRtcRead answer);
void fake_hal_set_wall(uint32_t utc_s, int16_t offset_minutes);
int fake_hal_rtc_read_count(void);
uint32_t fake_hal_rtc_written(void);
void fake_hal_set_idle_microseconds(uint32_t idle_us);
void fake_hal_set_core1_load(uint32_t elapsed_cycles, uint32_t idle_cycles);
void fake_hal_set_power_levels(uint8_t levels);
void fake_hal_set_power_samples(uint32_t samples);
bool fake_hal_supply_held_off(void);
int fake_hal_saved_report_count(void);
uint32_t fake_hal_saved_report(int index);
uint32_t fake_hal_script_mark(void);
const uint8_t *fake_hal_screenshot(const char **path, uint32_t *size);
void fake_hal_fill_profile(void);
int fake_hal_profile_reset_count(void);
void fake_hal_set_restart_pending(bool pending);
void fake_hal_break_module(uint32_t module, bool broken);
void fake_hal_evict_module(void);
uint32_t fake_hal_loaded_module(void);
int fake_hal_module_loads(void);
int fake_hal_module_refusals(void);
void fake_hal_set_reserve_result(HalReserveResult result);
int fake_hal_reserve_count(void);
uint32_t fake_hal_reserved_size(const char *path);
int fake_hal_scratch_borrow_count(void);
void fake_hal_refuse_scratch(bool refused);
int fake_hal_listing_borrow_count(void);
void fake_hal_set_empty_card(bool empty);
int fake_hal_dir_open_count(void);
int fake_hal_presents_at_last_dir_open(void);
int fake_hal_library_writes(void);
int fake_hal_covers_writes(void);
int fake_hal_scratch_outstanding(void);
int fake_hal_loop_bench_count(void);
int fake_hal_core1_test_count(void);
int fake_hal_core1_accel_count(void);
int fake_hal_audio_service_count(void);
HalBluetoothStatus *fake_hal_bluetooth(void);
bool fake_hal_bluetooth_powered(void);
int fake_hal_bluetooth_scan_count(void);
int fake_hal_bluetooth_stop_count(void);
int fake_hal_bluetooth_service_count(void);
int fake_hal_bluetooth_connect_count(void);
const HalBluetoothDevice *fake_hal_bluetooth_connect_target(void);
int fake_hal_bluetooth_disconnect_count(void);
void fake_hal_queue_transition(Key key, bool pressed, uint32_t time_ms);
void fake_hal_queue_headset_command(HalHeadsetCommand command);
uint16_t fake_hal_pixel(int x, int y);
const char *fake_hal_audio_playing_path(void);
uint32_t fake_hal_audio_play_start(void);
uint32_t fake_hal_audio_play_end(void);
uint32_t fake_hal_audio_queued_start(void);
uint32_t fake_hal_audio_queued_end(void);
int fake_hal_audio_play_count(void);
int fake_hal_audio_release_count(void);
const char *fake_hal_audio_queued_path(void);
void fake_hal_audio_finish_track(void);
void fake_hal_audio_fail_track(void);
void fake_hal_audio_refuse_plays(int count);
void fake_hal_audio_set_jack(HalAudioJack jack);
void fake_hal_audio_set_dac_identity(int chip, DacIdentity identity);
uint32_t fake_hal_dma_tone_rate(void);
void fake_hal_audio_set_refusal(HalAudioRefusal refusal);
void fake_hal_audio_set_position(uint32_t position_ms);
void fake_hal_audio_set_format(HalAudioFormat format);
void fake_hal_audio_set_token(uint32_t token);
int fake_hal_audio_seek_count(void);
uint32_t fake_hal_audio_last_seek(void);
uint8_t fake_hal_audio_volume(void);
int fake_hal_audio_state(void);
int fake_hal_write_count(void);

typedef struct FakeArea {
    int x;
    int y;
    int width;
    int height;
} FakeArea;

typedef struct FakeDisplayWrites {
    FakeArea first;
    int count;
    int narrowest;
    bool same;
} FakeDisplayWrites;

void fake_hal_forget_display_writes(void);
FakeDisplayWrites fake_hal_display_writes(void);
long fake_hal_pixels_written(void);
int fake_hal_present_count(void);
bool fake_hal_display_on(void);
HalDisplayRotation fake_hal_display_rotation(void);
int fake_hal_presents_at_power_on(void);
uint8_t fake_hal_display_brightness(void);
void fake_hal_set_battery(HalBattery battery);
int fake_hal_power_off_count(void);
int fake_hal_audio_state_at_power_off(void);
void fake_hal_set_usb_start(bool started);
int fake_hal_audio_gain(void);
int fake_hal_audio_output(void);
HalAudioEqualizer fake_hal_audio_equalizer(void);
int fake_hal_usb_mode(void);
HalUsbAnswer fake_hal_usb_answer(void);
int fake_hal_usb_mode_requests(void);
HalUsbMode fake_hal_requested_usb_mode(void);
int fake_hal_dac_volume(void);
const uint8_t *fake_hal_file_bytes(const char *path, uint32_t *size);

void fake_hal_fail_music_writes(bool fail);
void fake_hal_fail_file_writes(const char *path);
void fake_hal_fail_file_opens(const char *path);

void fake_hal_fail_file_reads(const char *path);

uint32_t fake_hal_audio_replay_gain(void);
uint32_t fake_hal_audio_queued_replay_gain(void);
uint32_t fake_hal_audio_queued_fade_ms(void);
uint32_t fake_hal_audio_play_fade_ms(void);

void fake_hal_set_card(HalCardInfo info, bool result);
int fake_hal_card_formats(void);

void fake_hal_fail_internal_writes(const char *path);
