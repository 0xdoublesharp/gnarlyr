#include "device/usb_audio_sink.h"

#include <stddef.h>

_Static_assert(offsetof(UsbAudioSink, stats) == sizeof(uint32_t *) + 5 * sizeof(uint32_t),
               "the emulator reads sink statistics before optional state");

enum {
    TARGET_DIVISOR = 50,
    WINDOW_DIVISOR = 250,
    SETTLE_DIVISOR = 1000,
    GUARD_DIVISOR = 50,
    WORDS_PER_FRAME = 2,
    WORD_BYTES = 4,
    PEAK_SHIFT = 16
};

void usb_audio_sink_init(UsbAudioSink *sink, uint32_t *ring, uint32_t ring_frames)
{
    *sink = (UsbAudioSink){
        .ring = ring, .ring_frames = ring_frames, .headphones = headphone_audio_defaults()};
}

void usb_audio_sink_start(UsbAudioSink *sink, uint32_t rate, uint8_t sample_bytes)
{
    sink->written = 0;
    sink->cleared = 0;
    sink->rate = rate;
    sink->feedback_rate = (uint32_t)(((uint64_t)rate << 16) / 8000);
    sink->feedback_started = false;
    sink->target = rate / (sink->asynchronous ? 200 : TARGET_DIVISOR);
    sink->guard = rate / (sink->asynchronous ? 1000 : GUARD_DIVISOR);
    sink->sample_bytes = sample_bytes;
    sink->playing = false;
    StreamDriftLimits limits = {(int32_t)sink->target, (int32_t)(rate / WINDOW_DIVISOR),
                                (int32_t)(rate / SETTLE_DIVISOR)};
    stream_drift_init(&sink->drift, limits);
}

void usb_audio_sink_set_playing(UsbAudioSink *sink, bool playing)
{
    sink->playing = playing;
}

void usb_audio_sink_set_headphones(UsbAudioSink *sink, HeadphoneAudio profile)
{
    sink->headphones = profile;
}

bool usb_audio_sink_primed(const UsbAudioSink *sink)
{
    return sink->sample_bytes != 0 && sink->written >= sink->target;
}

static void put_frame(UsbAudioSink *sink, uint32_t frame, uint32_t left, uint32_t right)
{
    uint32_t *words = sink->ring + (frame % sink->ring_frames) * WORDS_PER_FRAME;
    words[0] = left;
    words[1] = right;
}

static uint32_t sample_word(const uint8_t *bytes, uint8_t count)
{
    uint32_t word = 0;
    for (uint8_t index = 0; index < count; index++) {
        word |= (uint32_t)bytes[index] << (8 * (WORD_BYTES - count + index));
    }
    return word;
}

static void note_peak(UsbAudioSink *sink, int channel, uint32_t word)
{
    int32_t sample = (int32_t)word >> PEAK_SHIFT;
    uint16_t size = (uint16_t)(sample < 0 ? -sample : sample);
    if (size > sink->peaks[channel]) {
        sink->peaks[channel] = size;
    }
}

void usb_audio_sink_take_peaks(UsbAudioSink *sink, uint16_t *peaks)
{
    peaks[0] = sink->peaks[0];
    peaks[1] = sink->peaks[1];
    sink->peaks[0] = 0;
    sink->peaks[1] = 0;
}

static void clear_range(UsbAudioSink *sink, uint32_t frame, uint32_t end)
{
    for (; frame != end; frame++) {
        put_frame(sink, frame, 0, 0);
    }
}

static void clear_ahead(UsbAudioSink *sink)
{
    uint32_t end = sink->written + sink->guard;
    bool ahead = (int32_t)(sink->cleared - sink->written) > 0;
    clear_range(sink, ahead ? sink->cleared : sink->written, end);
    sink->cleared = end;
}

static void realign(UsbAudioSink *sink, uint32_t consumed)
{
    sink->written = consumed + sink->target;
    clear_range(sink, consumed, sink->written);
    sink->cleared = sink->written;
    sink->stats.realigned++;
}

static bool fill_out_of_range(const UsbAudioSink *sink, uint32_t frames, uint32_t consumed)
{
    int32_t fill = (int32_t)(sink->written - consumed);
    return fill < 0 || (uint32_t)fill + frames + 1 + sink->guard > sink->ring_frames;
}

static int32_t adjustment(UsbAudioSink *sink, uint32_t consumed)
{
    if (!sink->playing || sink->asynchronous) {
        return 0;
    }
    int32_t adjust = stream_drift_adjust(&sink->drift, (int32_t)(sink->written - consumed));
    sink->stats.dropped += adjust < 0 ? 1u : 0u;
    sink->stats.repeated += adjust > 0 ? 1u : 0u;
    return adjust;
}

static void write_frames(UsbAudioSink *sink, const uint8_t *bytes, uint32_t frames, int32_t adjust)
{
    uint32_t frame_bytes = 2u * sink->sample_bytes;
    uint32_t kept = adjust < 0 && frames > 1 ? frames - 1 : frames;
    uint32_t total = adjust > 0 ? kept + 1 : kept;
    for (uint32_t index = 0; index < total; index++) {
        const uint8_t *frame = bytes + (index < kept ? index : kept - 1) * frame_bytes;
        uint32_t left =
            headphone_audio_word(sample_word(frame, sink->sample_bytes), sink->headphones.left);
        uint32_t right = headphone_audio_word(
            sample_word(frame + sink->sample_bytes, sink->sample_bytes), sink->headphones.right);
        note_peak(sink, 0, left);
        note_peak(sink, 1, right);
        put_frame(sink, sink->written++, left, right);
    }
}

void usb_audio_sink_write(UsbAudioSink *sink, const uint8_t *bytes, uint32_t length,
                          uint32_t consumed)
{
    uint32_t frames = sink->sample_bytes == 0 ? 0 : length / (2u * sink->sample_bytes);
    if (frames == 0 || sink->sample_bytes > 4 ||
        frames + sink->target + sink->guard + 1 > sink->ring_frames) {
        return;
    }
    sink->stats.packets++;
    if (fill_out_of_range(sink, frames, consumed)) {
        sink->stats.underruns += (int32_t)(sink->written - consumed) < 0 ? 1u : 0u;
        sink->stats.overflows += (int32_t)(sink->written - consumed) >= 0 ? 1u : 0u;
        realign(sink, consumed);
    }
    write_frames(sink, bytes, frames, adjustment(sink, consumed));
    clear_ahead(sink);
}

void usb_audio_sink_set_asynchronous(UsbAudioSink *sink, bool asynchronous)
{
    sink->asynchronous = asynchronous;
}

uint32_t usb_audio_sink_feedback(UsbAudioSink *sink, uint32_t consumed, uint16_t tick)
{
    if (!sink->playing) {
        sink->feedback_started = false;
        return sink->feedback_rate;
    }
    if (!sink->feedback_started) {
        sink->feedback_consumed = consumed;
        sink->feedback_tick = tick;
        sink->feedback_started = true;
    }
    uint16_t elapsed = (uint16_t)((tick - sink->feedback_tick) & 0x3FFF);
    if (elapsed >= 1024) {
        uint32_t measured =
            (uint32_t)(((uint64_t)(consumed - sink->feedback_consumed) << 16) / elapsed);
        uint32_t nominal = (uint32_t)(((uint64_t)sink->rate << 16) / 8000);
        if (measured > nominal - nominal / 100 && measured < nominal + nominal / 100) {
            sink->feedback_rate = measured;
        }
        sink->feedback_consumed = consumed;
        sink->feedback_tick = tick;
    }
    int32_t error = (int32_t)(sink->written - consumed) - (int32_t)sink->target;
    int32_t limit = (int32_t)(sink->feedback_rate / 1000);
    int32_t correction = error < -limit / 8 ? -limit : error > limit / 8 ? limit : error * 8;
    return (uint32_t)((int32_t)sink->feedback_rate - correction);
}
