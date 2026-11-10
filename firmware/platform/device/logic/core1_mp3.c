#include "device/core1_mp3.h"

static void start_mp3(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                      const bool *starved)
{
    Core1Mp3 *mp3 = context;
    (void)info;
    (void)starved;
    mp3->next_sample = first_sample;
    mp3->tail_samples = mp3->control->tail_trim < MP3_MAX_TAIL_SAMPLES ? mp3->control->tail_trim
                                                                       : MP3_MAX_TAIL_SAMPLES;
    mp3->count = 0;
    mp3->head = 0;
    mp3->emitted = 0;
    mp3_decoder_start(mp3->decoder, input);
}

static Core1DecodeResult decode_pcm(Core1Mp3 *mp3, Core1Block *block)
{
    switch (mp3_decode(mp3->decoder)) {
    case MP3_DECODED:
        break;
    case MP3_STARVED:
        return CORE1_STARVED;
    case MP3_FINISHED:
        return CORE1_FINISHED;
    case MP3_FAILED:
        return CORE1_FAILED;
    }
    int channels = mp3_decoder_channels(mp3->decoder);
    block->channels[0] = mp3_decoder_channel(mp3->decoder, 0);
    block->channels[1] = mp3_decoder_channel(mp3->decoder, channels > 1 ? 1 : 0);
    block->samples = mp3_decoder_samples(mp3->decoder);
    block->position = mp3_decoder_position(mp3->decoder);
    return CORE1_DECODED;
}

static void retain_frame(Core1Mp3 *mp3, const Core1Block *block)
{
    uint32_t write = (mp3->head + mp3->count) % MP3_TAIL_CAPACITY;
    uint32_t part = MP3_TAIL_CAPACITY - write;
    part = part < block->samples ? part : block->samples;
    for (int channel = 0; channel < FLAC_MAX_CHANNELS; channel++) {
        for (uint32_t index = 0; index < part; index++) {
            mp3->tail[channel][write + index] = block->channels[channel][index];
        }
        for (uint32_t index = part; index < block->samples; index++) {
            mp3->tail[channel][index - part] = block->channels[channel][index];
        }
    }
    mp3->count += block->samples;
}

static Core1DecodeResult decode_trimmed(Core1Mp3 *mp3, Core1Block *block)
{
    mp3->count -= mp3->emitted;
    mp3->head = (mp3->head + mp3->emitted) % MP3_TAIL_CAPACITY;
    mp3->emitted = 0;
    while (mp3->count <= mp3->tail_samples) {
        Core1DecodeResult result = decode_pcm(mp3, block);
        if (result != CORE1_DECODED) {
            return result;
        }
        retain_frame(mp3, block);
    }
    uint32_t count = mp3->count - mp3->tail_samples;
    count = count < MP3_MAX_FRAME_SAMPLES ? count : MP3_MAX_FRAME_SAMPLES;
    mp3->emitted = count < MP3_TAIL_CAPACITY - mp3->head ? count : MP3_TAIL_CAPACITY - mp3->head;
    block->channels[0] = mp3->tail[0] + mp3->head;
    block->channels[1] = mp3->tail[1] + mp3->head;
    block->samples = mp3->emitted;
    block->position = mp3_decoder_position(mp3->decoder);
    return CORE1_DECODED;
}

static Core1DecodeResult decode_mp3(void *context, Core1Block *block)
{
    Core1Mp3 *mp3 = context;
    Core1DecodeResult result =
        mp3->tail_samples == 0 ? decode_pcm(mp3, block) : decode_trimmed(mp3, block);
    if (result == CORE1_DECODED) {
        block->first_sample = mp3->next_sample;
        mp3->next_sample += block->samples;
    }
    return result;
}

Core1Decoder core1_mp3_decoder(Core1Mp3 *mp3, Mp3Decoder *decoder,
                               volatile const StreamControl *control,
                               int32_t (*tail)[MP3_TAIL_CAPACITY])
{
    *mp3 = (Core1Mp3){.decoder = decoder, .control = control, .tail = tail};
    return (Core1Decoder){start_mp3, decode_mp3, mp3};
}
