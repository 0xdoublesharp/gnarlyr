#include <string.h>

#include "check.h"
#include "device/asf_stream.h"
#include "device/core1_wma.h"
#include "m4a_fixture_files.h"

static uint8_t records[65536];
static uint8_t original[65536];
static uint32_t record_bytes;
static Core1Wma decoder;
static struct {
    uint32_t before[4];
    WMADecodeContext value;
    uint32_t after[4];
} state;
static struct {
    uint32_t before[4];
    uint8_t bytes[WMA_PACKET_BYTES + 8];
    uint32_t after[4];
} packet;

static bool read_records(void *context, uint32_t offset, void *out, uint32_t length)
{
    (void)context;
    if (offset > record_bytes || length > record_bytes - offset) {
        return false;
    }
    memcpy(out, records + offset, length);
    return true;
}

static void load_records(void)
{
    static AsfStream stream;
    M4aFile file = m4a_file_load("wma/v2stereo.wma");
    AsfTrack track;
    CHECK(asf_track_probe(m4a_file_input(&file), &track));
    CHECK(asf_stream_start(&stream, m4a_file_input(&file), &track));
    record_bytes = 0;
    while (!stream.ended || stream.record_position < 4) {
        CHECK(record_bytes + 37 <= sizeof records);
        CHECK(asf_stream_read(&stream, record_bytes, records + record_bytes, 37));
        record_bytes += 37;
    }
    memcpy(original, records, record_bytes);
    m4a_file_free(&file);
}

static void bounded_mutations_do_not_escape_packet_or_context_storage(void)
{
    load_records();
    uint32_t random = 0x573131u;
    for (uint32_t trial = 0; trial < 256; trial++) {
        memcpy(records, original, record_bytes);
        random = random * 1664525u + 1013904223u;
        uint32_t at = WMA_CONFIG_BYTES + random % (record_bytes - WMA_CONFIG_BYTES);
        records[at] ^= (uint8_t)(1u << (trial % 8));
        memset(&state, 0x5A, sizeof state);
        memset(&packet, 0x5A, sizeof packet);
        Core1Decoder codec =
            core1_wma_decoder(&decoder, &state.value, packet.bytes, (StreamRegion){0});
        bool starved = false;
        codec.start(codec.context, (FlacInput){read_records, NULL, record_bytes}, (FlacInfo){0}, 0,
                    &starved);
        Core1DecodeResult result = CORE1_DECODED;
        for (uint32_t block = 0; block < 64 && result == CORE1_DECODED; block++) {
            Core1Block pcm;
            result = codec.decode(codec.context, &pcm);
            if (result == CORE1_DECODED) {
                CHECK(pcm.samples <= 2048);
            }
        }
        CHECK(result != CORE1_DECODED);
        for (uint32_t index = 0; index < 4; index++) {
            CHECK_EQUAL(0x5A5A5A5A, state.before[index]);
            CHECK_EQUAL(0x5A5A5A5A, state.after[index]);
            CHECK_EQUAL(0x5A5A5A5A, packet.before[index]);
            CHECK_EQUAL(0x5A5A5A5A, packet.after[index]);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(bounded_mutations_do_not_escape_packet_or_context_storage),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
