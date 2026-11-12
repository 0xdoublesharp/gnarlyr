#include <stdio.h>
#include <stdlib.h>

#include "device/ape_stream.h"

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FILE *file = context;
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, length, file) == length;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 3;
    }
    long length = ftell(file);
    ApeTrack track;
    ApeStream stream;
    uint32_t frame = (uint32_t)strtoul(argv[2], NULL, 10);
    if (length < 0 || (uint64_t)length > UINT32_MAX ||
        !ape_track_probe((FlacInput){read_file, file, (uint32_t)length}, &track) ||
        !ape_stream_start(&stream, (FlacInput){read_file, file, (uint32_t)length}, &track, frame)) {
        fclose(file);
        return 4;
    }
    uint32_t info[] = {track.channels, track.bits, track.rate, track.samples,
                       frame * track.frame_samples};
    if (fwrite(info, sizeof info, 1, stdout) != 1) {
        fclose(file);
        return 5;
    }
    while (!stream.ended || stream.record_position < APE_RECORD_BYTES) {
        uint8_t bytes[1024];
        if (!ape_stream_read(&stream, stream.position, bytes, sizeof bytes) ||
            fwrite(bytes, sizeof bytes, 1, stdout) != 1) {
            fclose(file);
            return 6;
        }
    }
    return fclose(file) == 0 ? 0 : 7;
}
