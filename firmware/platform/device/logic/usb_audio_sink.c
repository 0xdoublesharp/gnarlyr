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
    PEAK_SHIFT = 16,
    DOP64_RATE = 176400,
    DOP_MARKER_A = 0x05,
    DOP_MARKER_B = 0xFA,
    DOP_LOCK_FRAMES = 32
};

void usb_audio_sink_init(UsbAudioSink *sink, uint32_t *ring, uint32_t ring_frames)
{
    *sink = (UsbAudioSink){.ring = ring,
                           .ring_frames = ring_frames,
                           .unity = true,
                           .headphones = headphone_audio_defaults()};
}

void usb_audio_sink_start(UsbAudioSink *sink, uint32_t rate, uint8_t sample_bytes, bool dop)
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
    sink->dop = dop;
    sink->dop_pending = false;
    sink->dop_frames = 0;
    sink->dop_marker = 0;
    StreamDriftLimits limits = {(int32_t)sink->target, (int32_t)(rate / WINDOW_DIVISOR),
                                (int32_t)(rate / SETTLE_DIVISOR)};
    stream_drift_init(&sink->drift, limits);
}

void usb_audio_sink_set_headphones(UsbAudioSink *sink, HeadphoneAudio profile)
{
    sink->headphones = profile;
    sink->unity = profile.left == REPLAY_GAIN_UNITY && profile.right == REPLAY_GAIN_UNITY;
    if (!sink->unity) {
        sink->dop_frames = 0;
        sink->dop_marker = 0;
        sink->dop_pending |= sink->dop;
    }
}

static __attribute__((noinline)) uint32_t sample_word(const uint8_t *bytes, uint8_t count)
{
    uint32_t word = 0;
    for (uint8_t index = 0; index < count; index++) {
        word = (word >> 8) | (uint32_t)bytes[index] << 24;
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
    if (frame == end) {
        return;
    }
    uint32_t pos = frame % sink->ring_frames;
    uint32_t count = end - frame;
    uint32_t flip = sink->dop ? 0xFF000000u : 0;
    uint32_t word = sink->dop ? 0x05696900u : 0;
    if (frame & 1u) {
        word ^= flip;
    }
    for (uint32_t index = 0; index < count; index++) {
        uint32_t *words = sink->ring + pos * WORDS_PER_FRAME;
        words[0] = word;
        words[1] = word;
        word ^= flip;
        if (++pos == sink->ring_frames) {
            pos = 0;
        }
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
    uint32_t pos = sink->written % sink->ring_frames;
    bool pcm = !sink->dop;
    for (uint32_t index = 0; index < total; index++) {
        const uint8_t *frame = bytes + (index < kept ? index : kept - 1) * frame_bytes;
        uint32_t left = sample_word(frame, sink->sample_bytes);
        uint32_t right = sample_word(frame + sink->sample_bytes, sink->sample_bytes);
        if (pcm) {
            left = headphone_audio_word(left, sink->headphones.left);
            right = headphone_audio_word(right, sink->headphones.right);
            note_peak(sink, 0, left);
            note_peak(sink, 1, right);
        }
        uint32_t *words = sink->ring + pos * WORDS_PER_FRAME;
        words[0] = left;
        words[1] = right;
        if (++pos == sink->ring_frames) {
            pos = 0;
        }
        sink->written++;
    }
}

static __attribute__((noinline)) bool matches_mode(UsbAudioSink *sink, const uint8_t *bytes,
                                                   uint32_t frames)
{
    unsigned width = sink->sample_bytes;
    unsigned frame_bytes = width * WORDS_PER_FRAME;
    uint8_t previous = sink->dop_marker;
    unsigned streak = sink->dop_frames;
    for (; frames != 0; frames--, bytes += frame_bytes) {
        uint8_t marker = bytes[width - 1];
        if (marker != bytes[frame_bytes - 1] ||
            (marker != DOP_MARKER_A && marker != DOP_MARKER_B)) {
            marker = 0;
        }
        if (marker == 0 || marker == previous) {
            if (sink->dop) {
                return false;
            }
            streak = 0;
        }
        previous = marker;
        if (!sink->dop && marker != 0 && ++streak == DOP_LOCK_FRAMES) {
            return false;
        }
    }
    sink->dop_marker = previous;
    sink->dop_frames = (uint8_t)streak;
    return true;
}

static bool dop_allowed(const UsbAudioSink *sink)
{
    return sink->rate == DOP64_RATE && sink->asynchronous && sink->sample_bytes >= 3 && sink->unity;
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
    if (sink->dop_pending || (dop_allowed(sink) && !matches_mode(sink, bytes, frames))) {
        sink->dop_pending = true;
        return;
    }
    if (fill_out_of_range(sink, frames, consumed)) {
        sink->stats.underruns += (int32_t)(sink->written - consumed) < 0 ? 1u : 0u;
        sink->stats.overflows += (int32_t)(sink->written - consumed) >= 0 ? 1u : 0u;
        realign(sink, consumed);
    }
    if (sink->dop && (sink->written & 1u) != (bytes[sink->sample_bytes - 1] == DOP_MARKER_B)) {
        clear_range(sink, sink->written, sink->written + 1);
        sink->written++;
    }
    write_frames(sink, bytes, frames, adjustment(sink, consumed));
    clear_ahead(sink);
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
