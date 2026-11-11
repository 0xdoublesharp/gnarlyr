#include <stdio.h>
#include <stdlib.h>

#include "device/core1_stream.h"
#include "device/mp3_decoder.h"
#include "m4a_fixture_files.h"

static M4aFile opened;
static FlacDecoder unused_decoder;
static Mp3Decoder decoder;
static StreamControl control;
static Core1Source source;

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    opened = m4a_file_load(path);
    *input = m4a_file_input(&opened);
    return opened.size > 0;
}

static void close_file(void *context)
{
    (void)context;
    m4a_file_free(&opened);
}

static bool write_sample(FILE *output, int32_t sample)
{
    uint32_t value = (uint32_t)sample;
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    return fwrite(bytes, 1, sizeof bytes, output) == sizeof bytes;
}

static int dump(FILE *output)
{
    Mp3Result result;
    while ((result = mp3_decode(&decoder)) == MP3_DECODED) {
        for (uint32_t index = 0; index < mp3_decoder_samples(&decoder); index++) {
            for (int channel = 0; channel < mp3_decoder_channels(&decoder); channel++) {
                if (!write_sample(output, mp3_decoder_channel(&decoder, channel)[index])) {
                    return 1;
                }
            }
        }
    }
    return result == MP3_FINISHED ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        return 2;
    }
    core1_source_init(&source, (StreamRegion){.control = &control},
                      (FlacFileOpener){open_file, close_file, NULL}, &unused_decoder, 4096);
    AudioSource interface = core1_source_interface(&source, CORE1_FORMAT_MP3);
    AudioTrackInfo info;
    if (!interface.open(interface.context, argv[1], (uint32_t)strtoul(argv[2], NULL, 10), &info)) {
        return 1;
    }
    M4aFile audio = {opened.bytes + control.start_offset,
                     control.end_offset - control.start_offset};
    mp3_decoder_start(&decoder, m4a_file_input(&audio));
    FILE *output = fopen(argv[3], "wb");
    int status = output == NULL ? 1 : dump(output);
    if (output != NULL && fclose(output) != 0) {
        status = 1;
    }
    printf("%u %u %u %u %u %u\n", info.stream.channels, info.stream.sample_rate,
           control.first_sample, control.start_offset, control.end_offset, control.tail_trim);
    interface.close(interface.context);
    return status;
}
