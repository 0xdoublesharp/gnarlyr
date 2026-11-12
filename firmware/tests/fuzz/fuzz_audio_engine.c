#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/audio_engine.h"
#include "fuzz_source.h"

enum {
    CONTROL_BYTES = 8,
    ACTION_BYTES = 2,
    ACTION_LIMIT = 96,
    FRAME_SAMPLES = 16,
    RING_WORDS = 512
};

typedef struct ScriptedSource {
    int32_t left[FRAME_SAMPLES];
    int32_t right[FRAME_SAMPLES];
    uint32_t rates[2];
    uint32_t first_sample;
    uint32_t next_calls;
    uint8_t frames_per_open;
    uint8_t frames_left;
    uint8_t wait_calls;
    uint8_t error_call;
    uint8_t bits;
    uint8_t channels;
    bool fail_open;
} ScriptedSource;

static ScriptedSource source;
static bool prepare_resample_ok;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool open_track(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    ScriptedSource *script = context;
    if (script->fail_open) {
        return false;
    }
    uint32_t rate = path[0] == 'b' ? script->rates[1] : script->rates[0];
    script->frames_left = script->frames_per_open;
    script->first_sample = start_ms * rate / 1000u;
    script->next_calls = 0;
    *track = (AudioTrackInfo){.stream = {.sample_rate = rate,
                                         .channels = script->channels,
                                         .bits_per_sample = script->bits,
                                         .max_block = FRAME_SAMPLES},
                              .codec = AUDIO_CODEC_FLAC};
    return true;
}

static AudioFrameResult next_frame(void *context, AudioFrame *frame)
{
    ScriptedSource *script = context;
    script->next_calls++;
    if (script->error_call != 0 && script->next_calls == script->error_call) {
        return AUDIO_FRAME_ERROR;
    }
    if (script->wait_calls != 0 && script->next_calls <= script->wait_calls) {
        return AUDIO_FRAME_WAIT;
    }
    if (script->frames_left == 0) {
        return AUDIO_FRAME_END;
    }
    script->frames_left--;
    *frame = (AudioFrame){.left = script->left,
                          .right = script->channels == 1 ? script->left : script->right,
                          .first_sample = script->first_sample,
                          .samples = FRAME_SAMPLES,
                          .bits = script->bits};
    script->first_sample += FRAME_SAMPLES;
    return AUDIO_FRAME_READY;
}

static void release_frame(void *context)
{
    (void)context;
}

static void close_track(void *context)
{
    (void)context;
}

static bool prepare_resample(void)
{
    return prepare_resample_ok;
}

static uint32_t rate_at(uint8_t value)
{
    static const uint32_t RATES[] = {8000, 16000, 32000, 44100, 48000, 96000, 192000, 384000};
    return RATES[value % (sizeof RATES / sizeof RATES[0])];
}

static void check_engine(const AudioEngine *engine)
{
    fuzz_require(engine->segment_count >= 0 && engine->segment_count <= AUDIO_ENGINE_SEGMENTS);
    fuzz_require(engine->state >= AUDIO_ENGINE_STOPPED && engine->state <= AUDIO_ENGINE_PAUSED);
    AudioEngineStatus status = audio_engine_status(engine);
    fuzz_require(status.state >= AUDIO_ENGINE_STOPPED && status.state <= AUDIO_ENGINE_PAUSED);
    fuzz_require(audio_engine_buffered(engine, engine->consumed) <= RING_WORDS / 2);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t controls[CONTROL_BYTES] = {0};
    size_t controls_size = size < sizeof controls ? size : sizeof controls;
    memcpy(controls, data, controls_size);
    source = (ScriptedSource){.rates = {rate_at(controls[0]), rate_at(controls[1])},
                              .frames_per_open = (uint8_t)(1 + controls[2] % 8),
                              .wait_calls = controls[3] % 3,
                              .error_call = controls[4] % 12,
                              .bits = (controls[5] & 1u) != 0 ? 24 : 16,
                              .channels = (uint8_t)(1 + (controls[5] >> 1 & 1u)),
                              .fail_open = (controls[5] & 4u) != 0};
    for (int sample = 0; sample < FRAME_SAMPLES; sample++) {
        source.left[sample] = (sample + 1) * (controls[6] + 1);
        source.right[sample] = -source.left[sample];
    }
    prepare_resample_ok = (controls[5] & 8u) == 0;
    static uint32_t ring[RING_WORDS];
    static Equalizer equalizer;
    memset(ring, 0, sizeof ring);
    memset(&equalizer, 0, sizeof equalizer);
    AudioEngine engine;
    AudioSource interface = {open_track, next_frame, release_frame, close_track, &source};
    audio_engine_init(
        &engine, (AudioEngineConfig){.source = interface,
                                     .ring = ring,
                                     .ring_words = RING_WORDS,
                                     .equalizer = (controls[7] & 1u) != 0 ? &equalizer : NULL,
                                     .prepare_resample = prepare_resample,
                                     .resample_render =
                                         (controls[7] & 2u) != 0 ? NULL : audio_resample_render});
    uint32_t consumed = 0;
    const uint8_t *actions = size > CONTROL_BYTES ? data + CONTROL_BYTES : data + size;
    size_t action_count = size > CONTROL_BYTES ? (size - CONTROL_BYTES) / ACTION_BYTES : 0;
    if (action_count > ACTION_LIMIT) {
        action_count = ACTION_LIMIT;
    }
    for (size_t index = 0; index < action_count; index++) {
        uint8_t operation = actions[index * ACTION_BYTES];
        uint8_t value = actions[index * ACTION_BYTES + 1];
        AudioEngineTrack track = {.path = (value & 1u) != 0 ? "b" : "a",
                                  .start_ms = (uint32_t)(value & 3u) * 10,
                                  .end_ms = (value & 4u) != 0 ? 80 : 0,
                                  .token = value,
                                  .replay_gain = (value & 8u) != 0 ? 32768 : 65536,
                                  .fade_ms = (uint32_t)(value & 3u) * 5};
        switch (operation & 15u) {
        case 0:
            audio_engine_play(&engine, &track, (uint32_t)(value >> 4) * 5);
            break;
        case 1:
            audio_engine_queue_next(&engine, (value & 128u) != 0 ? NULL : &track);
            break;
        case 2:
            audio_engine_seek(&engine, (uint32_t)value * 10);
            break;
        case 3:
            audio_engine_pause(&engine);
            break;
        case 4:
            audio_engine_resume(&engine);
            break;
        case 5:
            audio_engine_stop(&engine);
            break;
        case 6:
            consumed += value;
            audio_engine_pump(&engine, consumed);
            break;
        case 7:
            consumed += value;
            audio_engine_observe(&engine, consumed);
            break;
        case 8:
            audio_engine_set_volume(&engine, value % (AUDIO_ENGINE_VOLUME_MAX + 1));
            break;
        case 9:
            audio_engine_set_headphones(&engine, (HeadphoneAudio){32768u + (uint32_t)value * 128u,
                                                                  65536u - (uint32_t)value * 128u,
                                                                  value % 61});
            break;
        case 10: {
            HalAudioEqualizer curve = {.band_count = value % (HAL_AUDIO_EQUALIZER_BANDS + 1),
                                       .pre_gain = value};
            for (int band = 0; band < curve.band_count; band++) {
                curve.band_hz[band] = (uint16_t)(100 + band * 1000 + value);
                curve.gain_half_db[band] = (int8_t)(value + band);
            }
            audio_engine_set_equalizer(&engine, &curve);
            break;
        }
        case 11:
            audio_engine_set_replay_gain(&engine, value, 16384u + (uint32_t)value * 512u);
            break;
        case 12: {
            uint32_t rate = 0;
            if (audio_engine_wants_rate(&engine, &rate) || (value & 1u) != 0) {
                audio_engine_restart(&engine);
            }
            break;
        }
        case 13:
            audio_engine_queue_needs_restart(&engine, (value & 1u) != 0 ? &track : NULL);
            break;
        case 14:
            audio_engine_current_path(&engine);
            audio_engine_refusal(&engine);
            audio_engine_stats(&engine);
            audio_engine_needs_full_clock(&engine);
            break;
        default:
            for (int pump = 0; pump < 4; pump++) {
                audio_engine_pump(&engine, consumed);
            }
            break;
        }
        check_engine(&engine);
    }
    audio_engine_stop(&engine);
    check_engine(&engine);
    return 0;
}
