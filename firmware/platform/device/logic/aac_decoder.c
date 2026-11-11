#include "device/aac_decoder.h"

#include <stddef.h>

#include "neaacdec.h"

bool aac_decoder_start(AacDecoder *decoder, const uint8_t *config, uint32_t size)
{
    if (config == NULL || size < 2 || (config[1] & 2u) != 0) {
        return false;
    }
    unsigned long rate = 0;
    unsigned char channels = 0;
    decoder->handle = NeAACDecOpen();
    if (decoder->handle == NULL) {
        return false;
    }
    NeAACDecConfigurationPtr setup = NeAACDecGetCurrentConfiguration(decoder->handle);
    setup->outputFormat = FAAD_FMT_24BIT;
    setup->defObjectType = LC;
    setup->downMatrix = 0;
    if (!NeAACDecSetConfiguration(decoder->handle, setup) ||
        NeAACDecInit2(decoder->handle, (unsigned char *)(uintptr_t)config, size, &rate,
                      &channels) != 0 ||
        channels < 1 || channels > 2) {
        return false;
    }
    NeAACDecPostSeekReset(decoder->handle, 1);
    decoder->channels = channels;
    decoder->samples = 0;
    return true;
}

bool aac_decoder_decode(AacDecoder *decoder, uint8_t *packet, uint32_t size)
{
    NeAACDecFrameInfo info;
    const int32_t *output = NeAACDecDecode(decoder->handle, &info, packet, size);
    uint32_t channels = decoder->channels;
    if (output == NULL || info.error != 0 || info.channels != channels ||
        info.samples > channels * AAC_FRAME_SAMPLES || info.samples % channels != 0) {
        return false;
    }
    decoder->samples = (uint32_t)info.samples / channels;
    for (uint32_t index = 0; index < decoder->samples; index++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            decoder->channel[channel][index] = output[index * channels + channel];
        }
    }
    return true;
}
