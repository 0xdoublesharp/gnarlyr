#include <math.h>
#include <stdio.h>

#include "check.h"
#include "sim_decoder.h"

static const char HI_RES_PATH[] = ECHO_FIXTURE_DIR "/flac/lpc_24_96k_stereo.flac";
static const char MONO_PATH[] = ECHO_FIXTURE_DIR "/flac/fixed_16_mono.flac";

static void a_flac_reports_its_stream_and_average_bitrate(void)
{
    SimDecoder decoder;
    CHECK(sim_decoder_open(&decoder, HI_RES_PATH));
    HalAudioFormat format = sim_decoder_format(&decoder);
    CHECK_EQUAL(HAL_AUDIO_CODEC_FLAC, format.codec);
    CHECK_EQUAL(96000, format.sample_rate);
    CHECK_EQUAL(24, format.bits_per_sample);
    CHECK_EQUAL(2, format.channels);
    CHECK_EQUAL(3917, format.bitrate_kbps);
    sim_decoder_close(&decoder);
}

static void a_mono_flac_reports_one_channel(void)
{
    SimDecoder decoder;
    CHECK(sim_decoder_open(&decoder, MONO_PATH));
    HalAudioFormat format = sim_decoder_format(&decoder);
    CHECK_EQUAL(44100, format.sample_rate);
    CHECK_EQUAL(16, format.bits_per_sample);
    CHECK_EQUAL(1, format.channels);
    sim_decoder_close(&decoder);
}

static void a_closed_decoder_has_no_format(void)
{
    SimDecoder decoder;
    CHECK(!sim_decoder_open(&decoder, ECHO_FIXTURE_DIR "/flac/missing.flac"));
    CHECK_EQUAL(HAL_AUDIO_CODEC_UNKNOWN, sim_decoder_format(&decoder).codec);
}

typedef struct OggReference {
    const char *directory;
    const char *name;
    const char *reference;
    uint32_t tolerance;
    uint32_t seek_settle;
    HalAudioCodec codec;
} OggReference;

static void compare_ogg_samples(SimDecoder *decoder, FILE *reference, const OggReference *fixture,
                                bool sought)
{
    float frames[73 * 2];
    uint64_t read;
    uint64_t compared = 0;
    while ((read = sim_decoder_read_stereo(decoder, frames, 73)) != 0) {
        for (uint64_t frame = 0; frame < read; frame++) {
            for (uint32_t channel = 0; channel < decoder->channels; channel++) {
                int low = fgetc(reference);
                int high = fgetc(reference);
                CHECK(low != EOF && high != EOF);
                int16_t expected = (int16_t)((uint32_t)low | (uint32_t)high << 8);
                if (!sought || compared + frame >= fixture->seek_settle) {
                    CHECK(fabsf(frames[frame * 2 + channel] * 32768 - expected) <=
                          (float)fixture->tolerance);
                }
            }
            if (decoder->channels == 1) {
                CHECK(frames[frame * 2] == frames[frame * 2 + 1]);
            }
        }
        compared += read;
    }
    CHECK_EQUAL(decoder->total_frames, decoder->frame);
    CHECK_EQUAL(EOF, fgetc(reference));
    CHECK(!decoder->ogg.failed);
}

static void compare_ogg_fixture(const OggReference *fixture)
{
    SimDecoder decoder;
    char path[512];
    const char *extension = fixture->codec == HAL_AUDIO_CODEC_OPUS ? "opus" : "ogg";
    snprintf(path, sizeof path, "%s/%s/%s.%s", ECHO_FIXTURE_DIR, fixture->directory, fixture->name,
             extension);
    bool opened = sim_decoder_open(&decoder, path);
    CHECK(opened);
    if (!opened) {
        return;
    }
    CHECK_EQUAL(fixture->codec, sim_decoder_format(&decoder).codec);
    snprintf(path, sizeof path, "%s/%s/%s.pcm", ECHO_FIXTURE_DIR, fixture->directory,
             fixture->reference);
    FILE *reference = fopen(path, "rb");
    CHECK(reference != NULL);
    if (reference != NULL) {
        uint64_t targets[] = {0, decoder.total_frames / 2, decoder.total_frames};
        for (uint32_t target = 0; target < 3; target++) {
            CHECK(sim_decoder_seek(&decoder, targets[target]));
            CHECK_EQUAL(0,
                        fseek(reference, (long)(targets[target] * decoder.channels * 2), SEEK_SET));
            compare_ogg_samples(&decoder, reference, fixture, target != 0);
        }
        CHECK(!sim_decoder_seek(&decoder, decoder.total_frames + 1));
        fclose(reference);
    }
    sim_decoder_close(&decoder);
}

static void ogg_vorbis_decodes_seeks_and_duplicates_mono_through_the_simulator(void)
{
    static const OggReference FIXTURES[] = {
        {"vorbis", "stereo44", "stereo44", 3, 0, HAL_AUDIO_CODEC_VORBIS},
        {"vorbis", "mono8", "mono8", 3, 0, HAL_AUDIO_CODEC_VORBIS},
        {"vorbis", "cropped44", "cropped44", 3, 0, HAL_AUDIO_CODEC_VORBIS},
        {"vorbis", "multiplexed44", "seek44", 3, 0, HAL_AUDIO_CODEC_VORBIS},
        {"vorbis", "chained44", "stereo44", 3, 0, HAL_AUDIO_CODEC_VORBIS},
    };
    for (uint32_t index = 0; index < sizeof FIXTURES / sizeof FIXTURES[0]; index++) {
        compare_ogg_fixture(&FIXTURES[index]);
    }
}

static void ogg_opus_uses_the_firmware_decoder_for_gain_trim_and_seek(void)
{
    static const OggReference FIXTURES[] = {
        {"opus", "stereo48", "stereo48", 16, 9600, HAL_AUDIO_CODEC_OPUS},
        {"opus", "speech16", "speech16", 16, 9600, HAL_AUDIO_CODEC_OPUS},
        {"opus", "long120", "long120", 16, 9600, HAL_AUDIO_CODEC_OPUS},
        {"opus", "gain120", "gain120", 16, 9600, HAL_AUDIO_CODEC_OPUS},
        {"opus", "origin48", "stereo48", 16, 9600, HAL_AUDIO_CODEC_OPUS},
    };
    for (uint32_t index = 0; index < sizeof FIXTURES / sizeof FIXTURES[0]; index++) {
        compare_ogg_fixture(&FIXTURES[index]);
    }
}

static void queued_ogg_decoders_keep_separate_codec_state(void)
{
    SimDecoder vorbis;
    SimDecoder opus;
    CHECK(sim_decoder_open(&vorbis, ECHO_FIXTURE_DIR "/vorbis/stereo44.ogg"));
    CHECK(sim_decoder_open(&opus, ECHO_FIXTURE_DIR "/opus/stereo48.opus"));
    float a[64 * 2];
    float b[64 * 2];
    for (uint32_t index = 0; index < 10; index++) {
        CHECK_EQUAL(64, sim_decoder_read_stereo(&vorbis, a, 64));
        CHECK_EQUAL(64, sim_decoder_read_stereo(&opus, b, 64));
    }
    CHECK_EQUAL(640, vorbis.frame);
    CHECK_EQUAL(640, opus.frame);
    sim_decoder_close(&vorbis);
    sim_decoder_close(&opus);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(queued_ogg_decoders_keep_separate_codec_state),
        TEST_CASE(ogg_opus_uses_the_firmware_decoder_for_gain_trim_and_seek),
        TEST_CASE(ogg_vorbis_decodes_seeks_and_duplicates_mono_through_the_simulator),
        TEST_CASE(a_flac_reports_its_stream_and_average_bitrate),
        TEST_CASE(a_mono_flac_reports_one_channel),
        TEST_CASE(a_closed_decoder_has_no_format),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
