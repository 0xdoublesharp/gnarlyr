#include "device/vorbis_decoder.h"

#include "codec_internal.h"
#include "core/seam.h"

typedef struct VorbisInput {
    ogg_buffer buffers[2];
    ogg_reference references[2];
    ogg_packet packet;
} VorbisInput;

static bool packet_input(VorbisInput *input, VorbisPacket packet, uint32_t number)
{
    if ((packet.granule != UINT64_MAX && packet.granule > INT64_MAX) ||
        packet.span[0].size > INT32_MAX || packet.span[1].size > INT32_MAX - packet.span[0].size) {
        return false;
    }
    *input = (VorbisInput){0};
    for (uint32_t index = 0; index < 2; index++) {
        VorbisSpan span = packet.span[index];
        if (span.size != 0 && span.bytes == NULL) {
            return false;
        }
        input->buffers[index].data = (unsigned char *)(uintptr_t)span.bytes;
        input->buffers[index].size = (long)span.size;
        input->references[index] =
            (ogg_reference){.buffer = &input->buffers[index], .length = (long)span.size};
    }
    input->references[0].next = packet.span[1].size != 0 ? &input->references[1] : NULL;
    uint32_t size = packet.span[0].size + packet.span[1].size;
    if (size == 0) {
        return false;
    }
    input->packet = (ogg_packet){
        .packet = &input->references[packet.span[0].size != 0 ? 0 : 1],
        .bytes = (long)size,
        .b_o_s = packet.first,
        .e_o_s = packet.last,
        .granulepos = packet.granule == UINT64_MAX ? -1 : (ogg_int64_t)packet.granule,
        .packetno = number,
    };
    return true;
}

static bool failed(VorbisDecoder *decoder)
{
    if (decoder->memory.error == VORBIS_MEMORY_OK) {
        decoder->memory.error = VORBIS_MEMORY_INVALID;
    }
    decoder->failed = true;
    decoder->ready = false;
    vorbis_memory_unbind();
    return false;
}

bool vorbis_decoder_init(VorbisDecoder *decoder, void *workspace, uint32_t capacity)
{
    *decoder = (VorbisDecoder){0};
    bool valid = vorbis_memory_init(&decoder->memory, workspace, capacity);
    decoder->failed = !valid;
    return valid;
}

static void prepare_info(VorbisDecoder *decoder)
{
    decoder->info = vorbis_memory_allocate(sizeof(vorbis_info));
    decoder->comment = vorbis_memory_allocate(sizeof(vorbis_comment));
    vorbis_info_init(decoder->info);
    vorbis_comment_init(decoder->comment);
}

bool vorbis_decoder_header(VorbisDecoder *decoder, VorbisPacket packet)
{
    VorbisInput input;
    if (decoder->failed || decoder->ready || decoder->packet_number >= 3 || packet.last ||
        packet.first != (decoder->packet_number == 0) ||
        !packet_input(&input, packet, decoder->packet_number)) {
        return false;
    }
    decoder->memory.strict_bits = true;
    if (!vorbis_memory_bind(&decoder->memory)) {
        return false;
    }
    if (VORBIS_MEMORY_TRY(&decoder->memory) != 0) {
        return failed(decoder);
    }
    if (decoder->packet_number == 0) {
        prepare_info(decoder);
    }
    if (vorbis_dsp_headerin(decoder->info, decoder->comment, &input.packet) != 0) {
        return failed(decoder);
    }
    const vorbis_info *info = decoder->info;
    if (info->channels < 1 || info->channels > VORBIS_CHANNELS || info->rate < 8000 ||
        info->rate > 192000) {
        return failed(decoder);
    }
    decoder->channels = (uint8_t)info->channels;
    decoder->rate = (uint32_t)info->rate;
    if (decoder->packet_number == 2) {
        decoder->dsp = vorbis_dsp_create(decoder->info);
        decoder->ready = true;
    }
    decoder->packet_number++;
    vorbis_memory_unbind();
    return true;
}

CORE_SEAM bool vorbis_decoder_decode(VorbisDecoder *decoder, VorbisPacket packet, VorbisPcm *pcm)
{
    VorbisInput input;
    pcm->frames = 0;
    if (!decoder->ready || decoder->failed || decoder->ended || packet.first ||
        pcm->channels[0] == NULL || (decoder->channels > 1 && pcm->channels[1] == NULL) ||
        pcm->capacity < (uint32_t)vorbis_info_blocksize(decoder->info, 1) / 2 ||
        !packet_input(&input, packet, decoder->packet_number)) {
        return false;
    }
    decoder->memory.strict_bits = false;
    if (!vorbis_memory_bind(&decoder->memory)) {
        return false;
    }
    if (VORBIS_MEMORY_TRY(&decoder->memory) != 0) {
        return failed(decoder);
    }
    vorbis_dsp_state *dsp = decoder->dsp;
    if (vorbis_dsp_synthesis(dsp, &input.packet, 1) != 0 || dsp->out_begin < 0 ||
        dsp->out_end < dsp->out_begin || (uint32_t)dsp->out_end > pcm->capacity) {
        return failed(decoder);
    }
    int frames = vorbis_dsp_pcmout(dsp, pcm->channels, (int)pcm->capacity);
    if (frames < 0 || (uint32_t)frames > pcm->capacity || vorbis_dsp_read(dsp, frames) != 0) {
        return failed(decoder);
    }
    pcm->frames = (uint32_t)frames;
    decoder->packet_number++;
    decoder->ended = packet.last;
    vorbis_memory_unbind();
    return true;
}
