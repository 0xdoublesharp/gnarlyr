#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/debug_loop.h"

#include "audio_dsp.h"
#include "audio_process.h"
#include "board.h"
#include "core/format.h"
#include "core/seam.h"
#include "core1_region.h"
#include "dac.h"
#include "device/audio_engine.h"
#include "device/audio_math.h"
#include "device/card_format.h"
#include "device/dac_sequence.h"
#include "device/dma_loop.h"
#include "device/dsd_stream.h"
#include "device/equalizer.h"
#include "device/flac_source.h"
#include "device/m4a_stream.h"
#include "device/mp3_probe.h"
#include "device/mp4_demux.h"
#include "device/wav_source.h"
#include "device/workspace_loan.h"
#include "dmac.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"

enum {
    RING_BLOCKS = 7,
    BLOCK_WORDS = 4000,
    RING_WORDS = RING_BLOCKS * BLOCK_WORDS,
    WORDS_PER_FRAME = 2,
    RING_FRAMES = RING_WORDS / WORDS_PER_FRAME,
    STRETCH_WORDS = 5 * BLOCK_WORDS,
    PREFILL_DIVISOR = 10,
    PREFILL_TIMEOUT_MS = 1000,
    FILL_BUDGET_BYTES = 8192,
    IDLE_SHUTDOWN_MS = 3000,
    AUDIO_CLOCK_MHZ = 200,
    TONE_PERIOD_SAMPLES = 100,
    TONE_AMPLITUDE = 9000,
    MILLISECONDS_PER_SECOND = 1000,
    JACK_POLL_MS = 10,
    SNIFF_BYTES = 32
};

typedef enum SourceKind {
    SOURCE_FLAC = CORE1_PROGRAM_FLAC,
    SOURCE_MP3 = CORE1_PROGRAM_MP3,
    SOURCE_M4A = CORE1_PROGRAM_M4A,
    SOURCE_VORBIS = CORE1_PROGRAM_VORBIS,
    SOURCE_OPUS = CORE1_PROGRAM_OPUS,
    SOURCE_DSD = CORE1_PROGRAM_DSD,
    SOURCE_APE = CORE1_PROGRAM_APE,
    SOURCE_WMA = CORE1_PROGRAM_WMA,
    SOURCE_WAV,
    SOURCE_UNKNOWN
} SourceKind;
_Static_assert((int)CORE1_FORMAT_FLAC == (int)SOURCE_FLAC &&
                   (int)CORE1_FORMAT_MP3 == (int)SOURCE_MP3 &&
                   (int)CORE1_FORMAT_M4A == (int)SOURCE_M4A &&
                   (int)CORE1_FORMAT_VORBIS == (int)SOURCE_VORBIS &&
                   (int)CORE1_FORMAT_OPUS == (int)SOURCE_OPUS &&
                   (int)CORE1_FORMAT_DSD == (int)SOURCE_DSD &&
                   (int)CORE1_FORMAT_APE == (int)SOURCE_APE &&
                   (int)CORE1_FORMAT_WMA == (int)SOURCE_WMA,
               "each core 1 source kind names its program and its opener");

static union {
    FlacDecoder flac;
    WavBuffers wav;
    M4aStream m4a;
    OggStream ogg;
    ApeStream ape;
    AsfStream asf;
    CardFormat format;
} decoding __attribute__((section(".high_ram")));
_Static_assert(sizeof(OggStream) <= sizeof(FlacDecoder),
               "Vorbis input must fit the existing decoder workspace");
_Static_assert(sizeof(CardFormat) <= sizeof(FlacDecoder),
               "the card formatter must fit the existing decoder workspace");
static WorkspaceLoan workspace_loan;
static Equalizer equalizer __attribute__((section(".high_ram")));
static uint32_t music_ring[RING_WORDS];
static WavSource wav_source;
static DsdStream dsd_stream;
static SourceKind source_kind;
static bool unknown_format;
static Core1Source core1_source;
static bool using_core1;
static bool resume_after_clock_change;
static bool paused_before_clock_change;
static uint32_t position_before_clock_change;
static AudioEngine engine;
_Static_assert(offsetof(AudioEngine, produced) == 48, "the emulator follows the audio ring cursor");
static DmaItem items[RING_BLOCKS];
static HalFile file;
static bool engine_ready;
static bool path_up;
static uint32_t path_rate;
static bool transferring;
static uint32_t consumed;
static uint32_t last_position;
static uint32_t ring_words;
static bool stopped_waiting;
static uint32_t stopped_since_ms;
static bool tone_active;
static char last_path[AUDIO_ENGINE_PATH_CAPACITY];
static uint8_t volume_level;
static uint8_t bluetooth_level;
static uint8_t extra_attenuation;
static HeadphoneAudio headphones;
static bool headphones_set;
static void (*stream_service)(void);
static uint32_t *stream_buffer;
static uint32_t stream_frames;
static bool volume_set;
static HalAudioOutput output_setting;
static uint32_t last_jack_poll_ms;
static HalAudioStats stats;
_Static_assert(offsetof(HalAudioStats, output_underruns) == 8,
               "the emulator follows the output underrun counter");
static uint32_t read_us_pending;
static uint32_t output_started_us;

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const HalFile *open = context;
    uint32_t started_us = hal_clock_microseconds();
    bool read = hal_file_read(*open, offset, buffer, length);
    uint32_t read_us = hal_clock_microseconds() - started_us;
    read_us_pending += read_us;
    stats.read_total_us += read_us;
    return read;
}

static uint32_t larger(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static void pump(uint32_t consumed_frames)
{
    uint32_t frames = audio_engine_stats(&engine).frames;
    read_us_pending = 0;
    uint32_t decode_started_us = hal_clock_microseconds();
    if (using_core1) {
        core1_source_fill(&core1_source);
    }
    audio_engine_pump(&engine, consumed_frames);
    uint32_t elapsed_us = hal_clock_microseconds() - decode_started_us;
    stats.busy_us += elapsed_us;
    if (audio_engine_stats(&engine).frames == frames) {
        return;
    }
    stats.blocks++;
    stats.decode_us = elapsed_us;
    stats.decode_max_us = larger(stats.decode_max_us, stats.decode_us);
    stats.decode_total_us += stats.decode_us;
    stats.read_us = read_us_pending;
    stats.read_max_us = larger(stats.read_max_us, stats.read_us);
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    if (!hal_file_open(&file, path)) {
        return false;
    }
    *input = (FlacInput){.read = read_file, .context = &file, .size = file.size};
    return true;
}

static void close_file(void *context)
{
    (void)context;
    hal_file_close(&file);
}

static bool tag_is(const uint8_t *bytes, const char *tag)
{
    return bytes[0] == (uint8_t)tag[0] && bytes[1] == (uint8_t)tag[1] &&
           bytes[2] == (uint8_t)tag[2] && bytes[3] == (uint8_t)tag[3];
}

static SourceKind kind_of(const char *path)
{
    FlacInput input;
    if (!open_file(NULL, path, &input)) {
        return SOURCE_FLAC;
    }
    uint8_t magic[SNIFF_BYTES] = {0};
    bool read = input.size >= SNIFF_BYTES && input.read(input.context, 0, magic, sizeof magic);
    close_file(NULL);
    if (!read || tag_is(magic, "fLaC")) {
        return SOURCE_FLAC;
    }
    if ((tag_is(magic, "RIFF") && tag_is(magic + 8, "WAVE")) ||
        (tag_is(magic, "FORM") && (tag_is(magic + 8, "AIFF") || tag_is(magic + 8, "AIFC")))) {
        return SOURCE_WAV;
    }
    if (tag_is(magic, "DSD ") || tag_is(magic, "FRM8")) {
        return SOURCE_DSD;
    }
    if (tag_is(magic, "OggS")) {
        return tag_is(magic + 28, "Opus") ? SOURCE_OPUS : SOURCE_VORBIS;
    }
    if (magic[0] == 0x30 && magic[1] == 0x26 && magic[2] == 0xB2 && magic[3] == 0x75) {
        return SOURCE_WMA;
    }
    if (tag_is(magic, "MAC ")) {
        return SOURCE_APE;
    }
    if (mp4_signature(magic)) {
        return SOURCE_M4A;
    }
    return mp3_signature(magic) ? SOURCE_MP3 : SOURCE_UNKNOWN;
}

CORE_SEAM bool board_audio_vorbis_wait(Core1Source *source)
{
    uint32_t started = hal_clock_milliseconds();
    while (hal_clock_milliseconds() - started < PREFILL_TIMEOUT_MS) {
        core1_source_fill(source);
        if (source->read_failed) {
            return false;
        }
        if (source->region.control->vorbis_config_generation == source->generation) {
            return true;
        }
        volatile StreamSlot *slot = &source->region.control->slots[0];
        if (slot->state == STREAM_SLOT_FULL && slot->generation == source->generation) {
            return false;
        }
        board_delay_ms(1);
    }
    return false;
}

static AudioSource chosen_source(void)
{
    if (source_kind == SOURCE_WAV) {
        return wav_source_interface(&wav_source);
    }
    bool flac = source_kind == SOURCE_FLAC || source_kind == SOURCE_UNKNOWN;
    return core1_source_interface(&core1_source,
                                  flac ? CORE1_FORMAT_FLAC : (Core1Format)source_kind);
}

static void remember_path(const char *path)
{
    uint32_t index = 0;
    while (path[index] != '\0' && index + 1 < AUDIO_ENGINE_PATH_CAPACITY) {
        last_path[index] = path[index];
        index++;
    }
    last_path[index] = '\0';
}

const char *board_audio_last_path(void)
{
    return last_path[0] != '\0' ? last_path : NULL;
}

static bool open_chosen(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    (void)context;
    remember_path(path);
    source_kind = kind_of(path);
    unknown_format = source_kind == SOURCE_UNKNOWN;
    if (unknown_format || !board_audio_process_prepare()) {
        return false;
    }
    if (source_kind != SOURCE_WAV) {
        using_core1 = board_core1_start_stream((Core1Program)source_kind);
        core1_source.clock_mhz = hal_system_clock_mhz();
        if (!using_core1) {
            return false;
        }
    }
    AudioSource source = chosen_source();
    return source.open(source.context, path, start_ms, track);
}

static AudioFrameResult next_chosen(void *context, AudioFrame *frame)
{
    (void)context;
    AudioSource source = chosen_source();
    return source.next(source.context, frame);
}

static void release_chosen(void *context)
{
    (void)context;
    AudioSource source = chosen_source();
    source.release(source.context);
}

static void close_chosen(void *context)
{
    (void)context;
    AudioSource source = chosen_source();
    source.close(source.context);
}

static uint32_t *ring(void)
{
    return music_ring;
}

static void ensure_engine(void)
{
    if (engine_ready) {
        return;
    }
    FlacFileOpener files = {.open = open_file, .close = close_file, .context = NULL};
    core1_source_init(&core1_source, core1_region(), files, &decoding.flac, FILL_BUDGET_BYTES);
    core1_source.m4a = &decoding.m4a;
    core1_source.ogg = &decoding.ogg;
    core1_source.ape = &decoding.ape;
    core1_source.asf = &decoding.asf;
    core1_source.wait_vorbis = board_audio_vorbis_wait;
    core1_source.dsd = &dsd_stream;
    wav_source_init(&wav_source, files, &decoding.wav);
    AudioSource source = {open_chosen, next_chosen, release_chosen, close_chosen, NULL};
    audio_engine_init(&engine, (AudioEngineConfig){.source = source,
                                                   .ring = ring(),
                                                   .ring_words = RING_WORDS,
                                                   .stretch_words = STRETCH_WORDS,
                                                   .equalizer = &equalizer,
                                                   .prepare_resample = board_audio_dsp_prepare,
                                                   .resample_render = board_audio_dsp_render});
    engine_ready = true;
}

void *hal_system_borrow_listing(uint32_t *size)
{
    board_audio_shut_down();
    *size = (uint32_t)sizeof music_ring;
    return music_ring;
}

static uint32_t ring_address(void)
{
    return (uint32_t)(uintptr_t)(stream_service != NULL && stream_buffer != NULL ? stream_buffer
                                                                                 : music_ring);
}

static void update_consumed(void)
{
    if (!transferring) {
        return;
    }
    uint32_t frames = ring_words / WORDS_PER_FRAME;
    uint32_t position =
        dma_loop_position(dmac_source(), ring_address(), ring_words) / WORDS_PER_FRAME;
    consumed += (position + frames - last_position) % frames;
    last_position = position;
}

static void serve_fade(void)
{
    if (stream_service != NULL) {
        stream_service();
    } else if (engine_ready && transferring && !tone_active) {
        update_consumed();
        pump(consumed);
    }
}

static bool output_is_suspended_and_muted(void)
{
    return dmac_suspended() && dac_path_is_muted();
}

static void sample_output_underrun(void)
{
    if ((transferring || tone_active) && dac_path_take_underrun() &&
        !output_is_suspended_and_muted()) {
        stats.output_underruns++;
    }
}

static void end_transfer(bool restarting)
{
    if (!transferring) {
        return;
    }
    if (restarting) {
        dac_path_fade_out();
    } else {
        dac_path_set_muted(true);
    }
    sample_output_underrun();
    dac_path_stop_transfer();
    dmac_stop();
    transferring = false;
}

static void stop_transfer(void)
{
    end_transfer(false);
}

static void restart_transfer(void)
{
    end_transfer(true);
}

static void stop_path(void)
{
    stop_transfer();
    if (path_up) {
        dac_path_stop();
        path_up = false;
    }
}

static bool ensure_path(uint32_t rate)
{
    if (path_up && path_rate == rate) {
        return true;
    }
    stop_path();
    if (dac_path_start(rate) != HAL_TONE_PLAYED) {
        return false;
    }
    path_up = true;
    path_rate = rate;
    dac_path_output_on();
    return true;
}

static bool start_transfer(uint32_t words, bool audible)
{
    ring_words = words;
    consumed = 0;
    last_position = 0;
    uint32_t blocks = (words + BLOCK_WORDS - 1) / BLOCK_WORDS;
    if (!dma_ring_items(items, blocks, ring_address(), words / blocks,
                        (uint32_t)(uintptr_t)items) ||
        !dmac_start_loop(&items[0])) {
        return false;
    }
    dac_path_start_transfer();
    debug_loop_boot(DEBUG_BOOT_AUDIO);
    (void)dac_path_take_underrun();
    transferring = true;
    dac_path_set_fade_service(serve_fade);
    if (audible) {
        dac_path_set_muted(false);
    }
    return true;
}

static bool start_output(bool audible)
{
    if (!ensure_path(audio_engine_rate(&engine))) {
        return false;
    }
    uint32_t started_ms = hal_clock_milliseconds();
    uint32_t prefill = audio_engine_rate(&engine) / PREFILL_DIVISOR;
    prefill = prefill < RING_FRAMES * 3 / 4 ? prefill : RING_FRAMES * 3 / 4;
    while (audio_engine_buffered(&engine, 0) < prefill &&
           hal_clock_milliseconds() - started_ms < PREFILL_TIMEOUT_MS) {
        pump(0);
    }
    stats = (HalAudioStats){0};
    output_started_us = hal_clock_microseconds();
    stopped_waiting = false;
    return start_transfer(engine.config.ring_words, audible);
}

static void stop_tone(void)
{
    if (tone_active) {
        stop_path();
        tone_active = false;
    }
}

static void ensure_audio_clock(void)
{
    if (hal_system_clock_mhz() < AUDIO_CLOCK_MHZ) {
        hal_system_set_clock_mhz(AUDIO_CLOCK_MHZ);
    }
}

static AudioEngineTrack engine_track(const HalAudioTrack *track)
{
    if (track == NULL) {
        return (AudioEngineTrack){.path = NULL};
    }
    return (AudioEngineTrack){.path = track->path,
                              .start_ms = track->start_ms,
                              .end_ms = track->end_ms,
                              .token = track->token,
                              .replay_gain = track->replay_gain,
                              .fade_ms = track->fade_ms};
}

CORE_SEAM bool hal_audio_play(const HalAudioTrack *track, uint32_t offset_ms,
                              const HalAudioTrack *next)
{
    if (!workspace_loan_claimable(&workspace_loan)) {
        return false;
    }
    ensure_engine();
    ensure_audio_clock();
    stop_tone();
    restart_transfer();
    AudioEngineTrack request = engine_track(track);
    if (!audio_engine_play(&engine, &request, offset_ms)) {
        return false;
    }
    AudioEngineTrack upcoming = engine_track(next);
    audio_engine_queue_next(&engine, next != NULL ? &upcoming : NULL);
    if (!start_output(true)) {
        audio_engine_stop(&engine);
        return false;
    }
    return true;
}

void hal_audio_queue_next(const HalAudioTrack *track)
{
    ensure_engine();
    HalAudioStatus status = hal_audio_status();
    AudioEngineTrack request = engine_track(track);
    bool restart = audio_engine_queue_needs_restart(&engine, track != NULL ? &request : NULL);
    bool paused = status.state == HAL_AUDIO_PAUSED;
    if (restart) {
        stop_transfer();
    }
    audio_engine_queue_next(&engine, track != NULL ? &request : NULL);
    if (!restart) {
        return;
    }
    if (audio_engine_status(&engine).state == AUDIO_ENGINE_STOPPED || !start_output(!paused)) {
        audio_engine_stop(&engine);
        return;
    }
    if (paused) {
        audio_engine_pause(&engine);
        dac_path_set_muted(true);
        dmac_suspend();
    }
}

void hal_audio_pause(void)
{
    ensure_engine();
    if (transferring) {
        dac_path_set_muted(true);
        sample_output_underrun();
        dmac_suspend();
    }
    audio_engine_pause(&engine);
}

void hal_audio_release(void)
{
    if (stream_service == NULL) {
        board_audio_shut_down();
    }
}

void hal_audio_resume(void)
{
    ensure_engine();
    if (transferring) {
        bool quiet = output_is_suspended_and_muted();
        dmac_resume();
        if (quiet) {
            (void)dac_path_take_underrun();
        }
        dac_path_set_muted(false);
    }
    audio_engine_resume(&engine);
}

static void seek_to(uint32_t position_ms, bool paused)
{
    ensure_engine();
    ensure_audio_clock();
    restart_transfer();
    if (!audio_engine_seek(&engine, position_ms) || !start_output(!paused)) {
        audio_engine_stop(&engine);
        return;
    }
    if (paused) {
        audio_engine_pause(&engine);
        dac_path_set_muted(true);
        dmac_suspend();
    }
}

CORE_SEAM void hal_audio_seek(uint32_t position_ms)
{
    ensure_engine();
    seek_to(position_ms, audio_engine_status(&engine).state == AUDIO_ENGINE_PAUSED);
}

CORE_SEAM CORE_COLD void hal_audio_set_speed(uint16_t speed)
{
    ensure_engine();
    if (speed == engine.speed) {
        return;
    }
    audio_engine_set_speed(&engine, speed);
    if (engine.state != AUDIO_ENGINE_STOPPED) {
        hal_audio_seek(audio_engine_status(&engine).position_ms);
    }
}

static DacCurve output_curve(void)
{
    if (board_bluetooth_routed()) {
        return DAC_CURVE_BLUETOOTH;
    }
    switch (output_setting) {
    case HAL_AUDIO_OUTPUT_SINGLE_ENDED:
        return DAC_CURVE_SINGLE_ENDED;
    case HAL_AUDIO_OUTPUT_BALANCED:
        return DAC_CURVE_BALANCED;
    case HAL_AUDIO_OUTPUT_AUTO:
        break;
    }
    return hal_audio_jack() == HAL_AUDIO_JACK_BALANCED ? DAC_CURVE_BALANCED
                                                       : DAC_CURVE_SINGLE_ENDED;
}

static uint8_t routed_level(void)
{
    if (board_bluetooth_routed()) {
        return bluetooth_level;
    }
    return headphone_audio_limit(hal_audio_headphones(), volume_level);
}

static void apply_volume(void)
{
    if (volume_set) {
        uint32_t value = dac_volume_value(routed_level(), output_curve()) + extra_attenuation;
        dac_path_set_volume(value < DAC_VOLUME_MUTED ? (uint8_t)value : DAC_VOLUME_MUTED);
    }
}

void hal_audio_set_volume(uint8_t level)
{
    volume_level = headphone_audio_limit(hal_audio_headphones(), level);
    volume_set = true;
    apply_volume();
}

void hal_audio_set_bluetooth_volume(uint8_t level)
{
    bluetooth_level = level < HAL_AUDIO_VOLUME_MAX ? level : HAL_AUDIO_VOLUME_MAX;
    apply_volume();
}

static HeadphoneAudio engine_headphones(void)
{
    return board_bluetooth_routed() ? headphone_audio_defaults() : hal_audio_headphones();
}

CORE_COLD HeadphoneAudio hal_audio_headphones(void)
{
    return headphones_set ? headphones : headphone_audio_defaults();
}

CORE_SEAM CORE_COLD void hal_audio_set_headphones(HeadphoneAudio profile)
{
    volume_level = headphone_audio_limit(profile, volume_level);
    apply_volume();
    headphones = profile;
    headphones_set = true;
    ensure_engine();
    audio_engine_set_headphones(&engine, engine_headphones());
    apply_volume();
}

CORE_SEAM void hal_audio_set_gain(HalAudioGain gain)
{
    dac_path_set_gain(gain == HAL_AUDIO_GAIN_HIGH);
}

CORE_SEAM void hal_audio_set_filter(HalAudioFilter filter)
{
    dac_path_set_filter((uint8_t)filter);
}

CORE_SEAM void hal_audio_set_output(HalAudioOutput output)
{
    bool restore = path_up && !dac_path_is_muted();
    if (restore) {
        dac_path_set_muted(true);
    }
    output_setting = output;
    apply_volume();
    if (restore) {
        dac_path_set_muted(false);
    }
}

void hal_audio_set_equalizer(const HalAudioEqualizer *curve)
{
    ensure_engine();
    audio_engine_set_equalizer(&engine, curve);
}

void hal_audio_set_effects(HalAudioEffects effects)
{
    audio_engine_set_effects(&engine, effects);
}

static HalAudioState state_of(AudioEngineState state)
{
    switch (state) {
    case AUDIO_ENGINE_PLAYING:
        return HAL_AUDIO_PLAYING;
    case AUDIO_ENGINE_PAUSED:
        return HAL_AUDIO_PAUSED;
    case AUDIO_ENGINE_STOPPED:
        return HAL_AUDIO_STOPPED;
    }
    return HAL_AUDIO_STOPPED;
}

static HalAudioCodec codec_of(AudioCodec codec)
{
    switch (codec) {
    case AUDIO_CODEC_WAV:
        return HAL_AUDIO_CODEC_WAV;
    case AUDIO_CODEC_MP3:
        return HAL_AUDIO_CODEC_MP3;
    case AUDIO_CODEC_AAC:
        return HAL_AUDIO_CODEC_AAC;
    case AUDIO_CODEC_ALAC:
        return HAL_AUDIO_CODEC_ALAC;
    case AUDIO_CODEC_VORBIS:
        return HAL_AUDIO_CODEC_VORBIS;
    case AUDIO_CODEC_AIFF:
        return HAL_AUDIO_CODEC_AIFF;
    case AUDIO_CODEC_DSD:
        return HAL_AUDIO_CODEC_DSD;
    case AUDIO_CODEC_WMA:
        return HAL_AUDIO_CODEC_WMA;
    case AUDIO_CODEC_APE:
        return HAL_AUDIO_CODEC_APE;
    case AUDIO_CODEC_OPUS:
        return HAL_AUDIO_CODEC_OPUS;
    case AUDIO_CODEC_FLAC:
        break;
    }
    return HAL_AUDIO_CODEC_FLAC;
}

static HalAudioProblem unreadable_problem(void)
{
    if (unknown_format) {
        return HAL_AUDIO_PROBLEM_FORMAT;
    }
    if (source_kind == SOURCE_WMA) {
        return HAL_AUDIO_PROBLEM_WMA;
    }
    if (source_kind == SOURCE_APE) {
        return HAL_AUDIO_PROBLEM_APE;
    }
    return source_kind == SOURCE_M4A ? HAL_AUDIO_PROBLEM_AAC_ONLY : HAL_AUDIO_PROBLEM_UNREADABLE;
}

HalAudioRefusal hal_audio_refusal(void)
{
    ensure_engine();
    AudioRefusal refusal = audio_engine_refusal(&engine);
    switch (refusal.problem) {
    case AUDIO_REFUSAL_RATE:
        return (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_RATE,
                                 .sample_rate = refusal.sample_rate};
    case AUDIO_REFUSAL_UNREADABLE:
        return (HalAudioRefusal){.problem = unreadable_problem()};
    case AUDIO_REFUSAL_NONE:
        break;
    }
    return (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_NONE};
}

static HalAudioFormat format_of(AudioTrackInfo track)
{
    FlacInfo stream = track.stream;
    if (stream.sample_rate == 0) {
        return (HalAudioFormat){.codec = HAL_AUDIO_CODEC_UNKNOWN};
    }
    return (HalAudioFormat){
        .codec = codec_of(track.codec),
        .sample_rate = stream.sample_rate,
        .bits_per_sample = stream.bits_per_sample,
        .channels = stream.channels,
        .bitrate_kbps = average_kbps(track.audio_bytes, stream.total_samples, stream.sample_rate),
    };
}

static bool output_idle(void)
{
    return !path_up && !board_core1_streaming();
}

HalAudioStatus hal_audio_status(void)
{
    ensure_engine();
    update_consumed();
    audio_engine_observe(&engine, consumed);
    AudioEngineStatus status = audio_engine_status(&engine);
    return (HalAudioStatus){.state = state_of(status.state),
                            .token = status.token,
                            .position_ms = status.position_ms,
                            .failed = status.failed,
                            .idle = output_idle(),
                            .format = format_of(status.track)};
}

static void change_rate(void)
{
    uint32_t rate = 0;
    if (!audio_engine_wants_rate(&engine, &rate)) {
        return;
    }
    restart_transfer();
    audio_engine_restart(&engine);
    if (!start_output(true)) {
        audio_engine_stop(&engine);
    }
}

static void shut_down_when_idle(void)
{
    if (audio_engine_status(&engine).state != AUDIO_ENGINE_STOPPED) {
        stopped_waiting = false;
        return;
    }
    uint32_t now_ms = hal_clock_milliseconds();
    if (!stopped_waiting) {
        stopped_waiting = true;
        stopped_since_ms = now_ms;
        return;
    }
    if (now_ms - stopped_since_ms >= IDLE_SHUTDOWN_MS) {
        stop_path();
        board_core1_stop();
        using_core1 = false;
        stopped_waiting = false;
    }
}

static void poll_jack(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    if (now_ms - last_jack_poll_ms < JACK_POLL_MS) {
        return;
    }
    last_jack_poll_ms = now_ms;
    if (board_jack_poll() && output_setting == HAL_AUDIO_OUTPUT_AUTO) {
        apply_volume();
    }
    if (!board_jack_settling() && hal_audio_jack() != HAL_AUDIO_JACK_NONE) {
        dac_path_guard(false);
    }
}

void hal_audio_service(void)
{
    poll_jack();
    sample_output_underrun();
    if (stream_service != NULL) {
        stream_service();
        return;
    }
    if (!engine_ready || !transferring || tone_active) {
        return;
    }
    update_consumed();
    pump(consumed);
    change_rate();
    shut_down_when_idle();
}

void hal_audio_reset_stats(void)
{
    stats = (HalAudioStats){0};
    output_started_us = hal_clock_microseconds();
}

HalAudioStats hal_audio_stats(void)
{
    HalAudioStats result = stats;
    if (!engine_ready || !transferring || tone_active) {
        return result;
    }
    result.underruns = audio_engine_stats(&engine).underruns;
    result.elapsed_us = hal_clock_microseconds() - output_started_us;
    uint32_t rate = audio_engine_rate(&engine);
    if (rate > 0) {
        result.buffered_ms = (uint32_t)((uint64_t)audio_engine_buffered(&engine, consumed) *
                                        MILLISECONDS_PER_SECOND / rate);
    }
    return result;
}

HalToneResult hal_audio_dma_tone_start(uint32_t sample_rate)
{
    ensure_engine();
    stop_tone();
    stop_transfer();
    audio_engine_stop(&engine);
    if (!dmac_present()) {
        return HAL_TONE_NO_DMAC;
    }
    stop_path();
    HalToneResult result = dac_path_start(sample_rate);
    if (result != HAL_TONE_PLAYED) {
        return result;
    }
    path_up = true;
    path_rate = sample_rate;
    uint32_t *words = ring();
    int16_t samples[TONE_PERIOD_SAMPLES];
    tone_fill(samples, TONE_PERIOD_SAMPLES, TONE_AMPLITUDE);
    for (uint32_t index = 0; index < RING_WORDS; index++) {
        uint32_t frame = index / WORDS_PER_FRAME;
        words[index] = (uint32_t)(uint16_t)samples[frame % TONE_PERIOD_SAMPLES] << 16;
    }
    dac_path_output_on();
    tone_active = true;
    if (!start_transfer(RING_WORDS, true)) {
        tone_active = false;
        stop_path();
        return HAL_TONE_NO_DMAC;
    }
    return HAL_TONE_PLAYED;
}

void hal_audio_dma_tone_stop(void)
{
    stop_tone();
}

HalDmaToneStatus hal_audio_dma_tone_status(void)
{
    if (!tone_active) {
        return (HalDmaToneStatus){.running = false, .position = 0};
    }
    uint32_t position = dma_loop_position(dmac_source(), ring_address(), ring_words);
    return (HalDmaToneStatus){.running = dmac_running(), .position = position};
}

const char *board_audio_current_path(void)
{
    return engine_ready ? audio_engine_current_path(&engine) : NULL;
}

void board_audio_before_clock_change(void)
{
    resume_after_clock_change = false;
    if (!board_core1_streaming()) {
        return;
    }
    if (engine_ready && transferring) {
        HalAudioStatus status = hal_audio_status();
        resume_after_clock_change = status.state != HAL_AUDIO_STOPPED;
        paused_before_clock_change = status.state == HAL_AUDIO_PAUSED;
        position_before_clock_change = status.position_ms;
        audio_engine_stop(&engine);
    }
    board_core1_stop();
    using_core1 = false;
}

void board_audio_after_clock_change(void)
{
    if (!resume_after_clock_change) {
        return;
    }
    resume_after_clock_change = false;
    seek_to(position_before_clock_change, paused_before_clock_change);
}

void board_audio_shut_down(void)
{
    if (engine_ready) {
        audio_engine_stop(&engine);
    }
    stop_tone();
    stop_path();
    board_core1_stop();
    using_core1 = false;
}

bool board_audio_converting(void)
{
    return engine_ready && transferring && audio_engine_needs_full_clock(&engine);
}

uint8_t board_audio_volume_level(void)
{
    return volume_level;
}

void board_audio_set_attenuation(uint8_t steps)
{
    extra_attenuation = steps;
    apply_volume();
}

static void clear_ring(void)
{
    uint32_t frames;
    uint32_t *buffer = board_audio_stream_ring(&frames);
    for (uint32_t index = 0; index < frames * WORDS_PER_FRAME; index++) {
        buffer[index] = 0;
    }
}

bool board_audio_stream_open(uint32_t rate, void (*service)(void))
{
    if (!workspace_loan_claimable(&workspace_loan)) {
        return false;
    }
    ensure_engine();
    ensure_audio_clock();
    board_audio_shut_down();
    stream_service = service;
    clear_ring();
    return ensure_path(rate);
}

uint32_t *board_audio_stream_ring(uint32_t *frames)
{
    *frames = stream_buffer != NULL ? stream_frames : RING_FRAMES;
    return stream_buffer != NULL ? stream_buffer : music_ring;
}

void board_audio_stream_buffer(uint32_t *buffer, uint32_t frames)
{
    stream_buffer = buffer;
    stream_frames = frames;
}

bool board_audio_stream_play(void)
{
    return start_transfer(stream_buffer != NULL ? stream_frames * WORDS_PER_FRAME : RING_WORDS,
                          true);
}

void board_audio_stream_pause(void)
{
    clear_ring();
    dac_path_stream_quiet();
    dac_path_stop_transfer();
    dmac_stop();
    transferring = false;
}

uint32_t board_audio_stream_consumed(void)
{
    update_consumed();
    return transferring ? consumed : 0;
}

void board_audio_stream_service(void)
{
    if (stream_service != NULL) {
        stream_service();
    }
}

void board_audio_stream_close(void)
{
    board_audio_shut_down();
    stream_service = NULL;
    stream_buffer = NULL;
    stream_frames = 0;
}

CORE_SEAM void hal_audio_set_replay_gain(uint32_t token, uint32_t gain)
{
    ensure_engine();
    audio_engine_set_replay_gain(&engine, token, gain);
}

static WorkspaceActivity audio_activity(void)
{
    bool playing = engine_ready && audio_engine_status(&engine).state != AUDIO_ENGINE_STOPPED;
    return (WorkspaceActivity){.playing = playing,
                               .stream_open = stream_service != NULL,
                               .output_up = path_up || transferring,
                               .core1_busy = using_core1 || board_core1_streaming(),
                               .tone = tone_active};
}

struct CardFormat *board_audio_lend_format(void)
{
    return workspace_loan_take(&workspace_loan, audio_activity()) ? &decoding.format : NULL;
}

void board_audio_return_format(void)
{
    workspace_loan_return(&workspace_loan);
}

bool board_audio_workspace_lent(void)
{
    return !workspace_loan_claimable(&workspace_loan);
}

CORE_SEAM bool board_audio_bluetooth_start(void)
{
    if (!engine_ready || !transferring || tone_active || stream_service != NULL) {
        return false;
    }
    uint32_t position = hal_audio_status().position_ms;
    restart_transfer();
    clear_ring();
    if (!audio_engine_seek(&engine, position)) {
        return false;
    }
    uint32_t length = audio_engine_rate(&engine) * 30u / 1000u;
    engine.fade_in = (AudioFadeRamp){.length = length,
                                     .reciprocal = length == 0 ? 0 : UINT32_MAX / length,
                                     .active = length != 0};
    apply_volume();
    return start_output(false);
}

void board_audio_output_changed(void)
{
    if (engine_ready) {
        audio_engine_set_headphones(&engine, engine_headphones());
    }
    apply_volume();
}

AudioVisualBuffer hal_audio_visual_buffer(void)
{
    if (!engine_ready || !transferring || tone_active || engine.state != AUDIO_ENGINE_PLAYING) {
        return (AudioVisualBuffer){0};
    }
    return (AudioVisualBuffer){music_ring, RING_FRAMES, engine.consumed % RING_FRAMES,
                               engine.produced - engine.consumed, audio_engine_rate(&engine)};
}

CORE_SEAM void hal_audio_set_sleep_remaining(uint32_t remaining_ms)
{
    audio_engine_set_sleep_remaining(&engine, remaining_ms);
}

CORE_SEAM uint8_t hal_audio_repeat_mark(bool clear)
{
    ensure_engine();
    update_consumed();
    audio_engine_observe(&engine, consumed);
    bool restart = engine.repeat_state != 0 || clear;
    bool paused = engine.state == AUDIO_ENGINE_PAUSED;
    if (restart) {
        stop_transfer();
    }
    uint8_t state = audio_engine_repeat_mark(&engine, clear);
    if (restart && !start_output(!paused)) {
        audio_engine_stop(&engine);
        return 0;
    }
    if (paused) {
        audio_engine_pause(&engine);
        dac_path_set_muted(true);
        dmac_suspend();
    }
    return state;
}
