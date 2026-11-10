#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

static int16_t samples[1024];
static void write_page(ogg_page *page)
{
    fwrite(page->header, 1, page->header_len, stdout);
    fwrite(page->body, 1, page->body_len, stdout);
}
static int encode(int argc, char **argv)
{
    if (argc != 4) {
        return 2;
    }
    int channels = atoi(argv[1]), rate = atoi(argv[2]);
    float quality = strtof(argv[3], NULL);
    if (channels < 1 || channels > 2 || rate < 8000) {
        return 2;
    }
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    ogg_stream_state stream;
    ogg_packet packet, id, tags, setup;
    ogg_page page;
    vorbis_info_init(&info);
    if (vorbis_encode_init_vbr(&info, channels, rate, quality)) {
        return 1;
    }
    vorbis_comment_init(&comment);
    vorbis_analysis_init(&dsp, &info);
    vorbis_block_init(&dsp, &block);
    ogg_stream_init(&stream, 42);
    vorbis_analysis_headerout(&dsp, &comment, &id, &tags, &setup);
    ogg_stream_packetin(&stream, &id);
    ogg_stream_packetin(&stream, &tags);
    ogg_stream_packetin(&stream, &setup);
    while (ogg_stream_flush(&stream, &page)) {
        write_page(&page);
    }
    int eof = 0;
    while (!eof) {
        size_t frames = fread(samples, channels * sizeof(int16_t), 512, stdin);
        if (!frames) {
            eof = 1;
        }
        float **buffer = vorbis_analysis_buffer(&dsp, (int)frames);
        for (size_t i = 0; i < frames; i++) {
            for (int ch = 0; ch < channels; ch++) {
                buffer[ch][i] = samples[i * channels + ch] / 32768.f;
            }
        }
        vorbis_analysis_wrote(&dsp, (int)frames);
        while (vorbis_analysis_blockout(&dsp, &block) == 1) {
            vorbis_analysis(&block, NULL);
            vorbis_bitrate_addblock(&block);
            while (vorbis_bitrate_flushpacket(&dsp, &packet)) {
                ogg_stream_packetin(&stream, &packet);
                while (ogg_stream_pageout(&stream, &page)) {
                    write_page(&page);
                }
            }
        }
    }
    return ferror(stdout) ? 1 : 0;
}

static int decode(void)
{
    OggVorbis_File file;
    if (ov_open_callbacks(stdin, &file, NULL, 0, OV_CALLBACKS_NOCLOSE) < 0) {
        return 1;
    }
    int section = 0;
    float **pcm;
    long frames;
    while ((frames = ov_read_float(&file, &pcm, 512, &section)) > 0) {
        vorbis_info *info = ov_info(&file, section);
        for (long frame = 0; frame < frames; frame++) {
            for (int channel = 0; channel < info->channels; channel++) {
                int value = (int)floorf(pcm[channel][frame] * 32767.f + 0.5f);
                value = value > 32767 ? 32767 : value;
                value = value < -32768 ? -32768 : value;
                if (fputc(value & 255, stdout) == EOF || fputc((value >> 8) & 255, stdout) == EOF) {
                    ov_clear(&file);
                    return 1;
                }
            }
        }
    }
    ov_clear(&file);
    return frames < 0 || ferror(stdout) ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "decode") == 0) {
        return decode();
    }
    if (argc == 5 && strcmp(argv[1], "encode") == 0) {
        return encode(argc - 1, argv + 1);
    }
    return 2;
}
