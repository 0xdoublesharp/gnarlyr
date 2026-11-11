#include "sim_ogg.h"

#include "device/core1_ogg.h"
#include "device/opus_track.h"

_Static_assert(OPUS_RING_BYTES + sizeof(OpusWorkspace) <=
                   VORBIS_RING_BYTES + VORBIS_WORKSPACE_BYTES,
               "the simulator arena must fit both Ogg decoders");

static bool read_file(void *context, uint32_t offset, void *out, uint32_t bytes)
{
    SimOgg *ogg = context;
    return fseek(ogg->file, (long)offset, SEEK_SET) == 0 &&
           fread(out, 1, bytes, ogg->file) == bytes;
}

static FlacInput input(SimOgg *ogg)
{
    return (FlacInput){read_file, ogg, ogg->track.file_bytes};
}

static bool normalize_vorbis(SimOgg *ogg)
{
    VorbisDecoder *decoder = &ogg->codec.vorbis.decoder;
    if (!ogg_stream_start(&ogg->stream, input(ogg)) ||
        !vorbis_decoder_init(decoder, ogg->ring + VORBIS_RING_BYTES, VORBIS_WORKSPACE_BYTES)) {
        return false;
    }
    FlacInput records = {ogg_stream_read, &ogg->stream, OGG_VIRTUAL_END};
    uint32_t position = 0;
    for (uint32_t index = 0; index < 3; index++) {
        OggRecord record;
        uint8_t *bytes = ogg->codec.vorbis.packet;
        if (core1_ogg_fetch(records, position, bytes, &record) != CORE1_DECODED) {
            return false;
        }
        VorbisPacket packet = {.span = {{bytes, record.bytes}, {NULL, 0}},
                               .granule = record.granule,
                               .first = (record.flags & OGG_RECORD_FIRST) != 0};
        if (!vorbis_decoder_header(decoder, packet)) {
            return false;
        }
        position += OGG_RECORD_BYTES + record.bytes;
    }
    VorbisModes modes;
    return vorbis_decoder_modes(decoder, &modes) &&
           ogg_stream_prepare_vorbis(&ogg->stream, &ogg->track, &modes, 0);
}

bool sim_ogg_seek(SimOgg *ogg, uint64_t frame)
{
    if (ogg->file == NULL || frame > ogg->track.samples) {
        return false;
    }
    uint32_t offset;
    bool sought = ogg->track.opus
                      ? opus_track_seek(input(ogg), &ogg->track, (uint32_t)frame, &offset)
                      : ogg_track_seek(input(ogg), &ogg->track, (uint32_t)frame, &offset);
    if (!sought || !ogg_stream_resume(&ogg->stream, input(ogg), &ogg->track, offset)) {
        return false;
    }
    ogg->control = (StreamControl){.generation = 1, .channels = ogg->track.channels};
    StreamRegion region = {.control = &ogg->control,
                           .samples = ogg->samples,
                           .ring = ogg->ring,
                           .ring_bytes = ogg->track.opus ? OPUS_RING_BYTES : VORBIS_RING_BYTES};
    ogg->decoder = ogg->track.opus ? core1_opus_decoder(&ogg->codec.opus, region)
                                   : core1_vorbis_decoder(&ogg->codec.vorbis, region);
    FlacInput records = {ogg_stream_read, &ogg->stream, OGG_VIRTUAL_END};
    ogg->decoder.start(ogg->decoder.context, records, (FlacInfo){0},
                       offset == 0 ? 0 : (uint32_t)frame, NULL);
    ogg->block = (Core1Block){0};
    ogg->taken = 0;
    ogg->next = (uint32_t)frame;
    ogg->failed = false;
    return true;
}

bool sim_ogg_open(SimOgg *ogg, const char *path)
{
    ogg->file = fopen(path, "rb");
    if (ogg->file == NULL) {
        return false;
    }
    long size = fseek(ogg->file, 0, SEEK_END) == 0 ? ftell(ogg->file) : -1;
    ogg->track = (OggTrack){0};
    bool opened = size > 0 && (unsigned long)size <= UINT32_MAX;
    if (opened) {
        FlacInput file = {read_file, ogg, (uint32_t)size};
        opened = opus_track_probe(file, &ogg->track) ||
                 (vorbis_track_probe(file, &ogg->track) && normalize_vorbis(ogg));
    }
    if (!opened || !sim_ogg_seek(ogg, 0)) {
        sim_ogg_close(ogg);
        return false;
    }
    return true;
}

static bool next_block(SimOgg *ogg)
{
    while (ogg->taken == ogg->block.samples) {
        Core1DecodeResult result = ogg->decoder.decode(ogg->decoder.context, &ogg->block);
        if (result != CORE1_DECODED) {
            ogg->failed = result != CORE1_FINISHED || ogg->next != ogg->track.samples;
            return false;
        }
        uint32_t before =
            ogg->next > ogg->block.first_sample ? ogg->next - ogg->block.first_sample : 0;
        ogg->taken = before < ogg->block.samples ? before : ogg->block.samples;
        if (ogg->taken < ogg->block.samples && ogg->block.first_sample + ogg->taken != ogg->next) {
            ogg->failed = true;
            return false;
        }
    }
    return true;
}

uint64_t sim_ogg_read(SimOgg *ogg, float *frames, uint64_t count)
{
    uint64_t written = 0;
    while (!ogg->failed && written < count && ogg->next < ogg->track.samples && next_block(ogg)) {
        uint32_t left = ogg->block.samples - ogg->taken;
        uint32_t end = ogg->track.samples - ogg->next;
        left = left < end ? left : end;
        uint32_t take = count - written < left ? (uint32_t)(count - written) : left;
        for (uint32_t frame = 0; frame < take; frame++) {
            for (uint32_t channel = 0; channel < ogg->track.channels; channel++) {
                frames[(written + frame) * ogg->track.channels + channel] =
                    (float)ogg->block.channels[channel][ogg->taken + frame] / 8388608.0f;
            }
        }
        ogg->taken += take;
        ogg->next += take;
        written += take;
    }
    return written;
}

void sim_ogg_close(SimOgg *ogg)
{
    if (ogg->file != NULL) {
        fclose(ogg->file);
        ogg->file = NULL;
    }
}
