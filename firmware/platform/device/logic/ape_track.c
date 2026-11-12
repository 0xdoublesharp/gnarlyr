#include "device/ape_track.h"

#include <stddef.h>

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint16_t little16(const uint8_t *bytes)
{
    return (uint16_t)((uint32_t)bytes[0] | (uint32_t)bytes[1] << 8);
}

static bool modern(FlacInput input, const uint8_t *head, ApeTrack *track)
{
    uint32_t descriptor = little32(head + 8);
    uint32_t header = little32(head + 12);
    uint32_t seeks = little32(head + 16);
    uint64_t start = (uint64_t)descriptor + header + seeks + little32(head + 20);
    uint64_t end = start + little32(head + 24);
    uint8_t format[24];
    if (descriptor < 52 || header < sizeof format || start >= input.size || end > input.size ||
        little32(head + 28) != 0 || !input.read(input.context, descriptor, format, sizeof format)) {
        return false;
    }
    track->seek_offset = descriptor + header;
    track->audio_start = (uint32_t)start;
    track->audio_end = (uint32_t)end;
    track->level = little16(format);
    track->frame_samples = little32(format + 4);
    track->final_samples = little32(format + 8);
    track->frames = little32(format + 12);
    track->bits = little16(format + 16);
    track->channels = little16(format + 18);
    track->rate = little32(format + 20);
    return (uint64_t)track->frames * 4 <= seeks;
}

static bool legacy(FlacInput input, const uint8_t *head, ApeTrack *track)
{
    uint32_t flags = little16(head + 8);
    uint32_t header = 32 + ((flags & 4) != 0 ? 4u : 0u);
    uint32_t seeks = little32(head + header);
    track->level = little16(head + 6);
    track->channels = little16(head + 10);
    track->rate = little32(head + 12);
    track->frames = little32(head + 24);
    track->final_samples = little32(head + 28);
    track->frame_samples = 73728 * 4;
    track->bits = (flags & 8) != 0 ? 24 : 16;
    if ((flags & 1) != 0 || ((flags & 16) != 0 && seeks < track->frames)) {
        return false;
    }
    header += (flags & 16) != 0 ? 4u : 0u;
    uint64_t seek = (uint64_t)header + little32(head + 16);
    uint64_t start = seek + (uint64_t)((flags & 16) != 0 ? seeks : track->frames) * 4;
    if (start >= input.size || little32(head + 20) >= input.size - start) {
        return false;
    }
    track->seek_offset = (uint32_t)seek;
    track->audio_start = (uint32_t)start;
    track->audio_end = input.size - little32(head + 20);
    return true;
}

static bool seek_entry(FlacInput input, const ApeTrack *track, uint32_t frame, uint32_t *offset)
{
    uint8_t entry[4];
    uint64_t at = (uint64_t)track->seek_offset + (uint64_t)frame * 4;
    if (at > input.size || sizeof entry > input.size - at ||
        !input.read(input.context, (uint32_t)at, entry, sizeof entry)) {
        return false;
    }
    *offset = little32(entry);
    return *offset >= track->audio_start && *offset < track->audio_end;
}

static bool validate_seeks(FlacInput input, const ApeTrack *track)
{
    uint32_t previous = 0;
    for (uint32_t frame = 0; frame < track->frames; frame++) {
        uint32_t offset;
        if (!seek_entry(input, track, frame, &offset) || offset <= previous ||
            (frame == 0 && offset != track->audio_start)) {
            return false;
        }
        previous = offset;
    }
    return true;
}

bool ape_track_probe(FlacInput input, ApeTrack *track)
{
    uint8_t head[52];
    ApeTrack candidate = {0};
    if (track == NULL || input.read == NULL || input.size < sizeof head ||
        !input.read(input.context, 0, head, sizeof head) || little32(head) != 0x2043414Du) {
        return false;
    }
    candidate.version = little16(head + 4);
    if (candidate.version < 3970 || candidate.version > 3990 ||
        !(candidate.version >= 3980 ? modern(input, head, &candidate)
                                    : legacy(input, head, &candidate)) ||
        !ape_track_allowed(&candidate) || candidate.frames == 0 || candidate.frame_samples == 0 ||
        candidate.frame_samples > 73728 * 16 || candidate.final_samples == 0 ||
        candidate.final_samples > candidate.frame_samples) {
        return false;
    }
    uint64_t samples =
        (uint64_t)(candidate.frames - 1) * candidate.frame_samples + candidate.final_samples;
    if (samples > UINT32_MAX || !validate_seeks(input, &candidate)) {
        return false;
    }
    candidate.samples = (uint32_t)samples;
    *track = candidate;
    return true;
}

bool ape_track_frame(FlacInput input, const ApeTrack *track, uint32_t frame, ApeFrame *out)
{
    uint32_t start;
    uint32_t end = track->audio_end;
    if (out == NULL || frame >= track->frames || !seek_entry(input, track, frame, &start) ||
        (frame + 1 < track->frames && !seek_entry(input, track, frame + 1, &end)) || end <= start) {
        return false;
    }
    uint32_t skip = (start - track->audio_start) & 3u;
    uint32_t bytes = end - start + skip;
    uint32_t rounded = ((bytes + 3u) & ~3u) + 4u;
    uint32_t available = input.size - (start - skip);
    if (rounded < bytes || bytes > available) {
        return false;
    }
    *out = (ApeFrame){.offset = start - skip,
                      .bytes = rounded,
                      .skip = skip,
                      .samples =
                          frame + 1 == track->frames ? track->final_samples : track->frame_samples};
    return true;
}

typedef struct ApePlaybackLimit {
    uint16_t level;
    uint32_t mono_rate;
    uint32_t stereo_rate;
    uint32_t boosted_stereo_rate;
} ApePlaybackLimit;

static uint32_t clock_for(const ApePlaybackLimit *limit, const ApeTrack *track)
{
    bool mono = track->channels == 1;
    uint32_t base = mono ? limit->mono_rate : limit->stereo_rate;
    uint32_t boosted = mono ? limit->mono_rate : limit->boosted_stereo_rate;
    return track->rate <= base      ? APE_LIMIT_BASE_MHZ
           : track->rate <= boosted ? APE_LIMIT_BOOST_MHZ
                                    : 0;
}

uint32_t ape_track_clock(const ApeTrack *track)
{
    static const ApePlaybackLimit LIMITS[] = {
        {1000, 192000, 96000, 192000},
        {2000, 192000, 48000, 96000},
        {3000, 96000, 48000, 48000},
    };
    if (!ape_track_allowed(track)) {
        return 0;
    }
    for (uint32_t index = 0; index < sizeof LIMITS / sizeof LIMITS[0]; index++) {
        if (track->level == LIMITS[index].level) {
            return clock_for(&LIMITS[index], track);
        }
    }
    return 0;
}

bool ape_track_realtime(const ApeTrack *track, uint32_t clock_mhz)
{
    uint32_t needed = ape_track_clock(track);
    return needed != 0 && needed <= clock_mhz;
}
