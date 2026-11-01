#include <stdio.h>
#include <stdlib.h>

#include "device/m4a_stream.h"
#include "device/mp4_demux.h"
#include "m4a_fixture_files.h"

enum {
    CHUNK = 4096,
    TAIL_ZEROS = 64
};

static M4aStream stream;

static bool finished(const M4aStream *source)
{
    return source->ended && source->record_done == source->head_size;
}

static int dump(FILE *output)
{
    uint8_t chunk[CHUNK];
    uint32_t offset = 0;
    while (!finished(&stream)) {
        if (!m4a_stream_read(&stream, offset, chunk, CHUNK)) {
            return 1;
        }
        fwrite(chunk, 1, CHUNK, output);
        offset += CHUNK;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: m4a_stream_tool FILE PACKET OUTPUT\n");
        return 2;
    }
    M4aFile file = m4a_file_load(argv[1]);
    Mp4Track track;
    FlacInput input = m4a_file_input(&file);
    if (file.size == 0 || !mp4_probe(input, &track) ||
        !m4a_stream_start(&stream, input, &track, (uint32_t)atoi(argv[2]))) {
        return 1;
    }
    FILE *output = fopen(argv[3], "wb");
    if (output == NULL || dump(output) != 0) {
        return 1;
    }
    fclose(output);
    printf("%u %u %u %u %u %u\n", track.codec, track.channels, track.sample_rate,
           track.frame_samples, track.delay, track.valid_samples);
    return 0;
}
