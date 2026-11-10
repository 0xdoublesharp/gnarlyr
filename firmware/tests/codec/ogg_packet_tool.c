#include <stdio.h>

#include "codec/ogg_packet.h"

enum {
    PACKET_BYTES = 65536
};

static uint8_t packet_buffer[PACKET_BYTES];

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FILE *file = context;
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, length, file) == length;
}

static bool write_number(FILE *file, uint64_t value, uint32_t bytes)
{
    for (uint32_t index = 0; index < bytes; index++) {
        if (fputc((uint8_t)(value >> (index * 8)), file) == EOF) {
            return false;
        }
    }
    return true;
}

static bool write_packets(FILE *file, FILE *output)
{
    if (fseek(file, 0, SEEK_END) != 0) {
        return false;
    }
    long length = ftell(file);
    if (length < 0 || (uint64_t)length > UINT32_MAX) {
        return false;
    }
    OggPackets reader;
    ogg_packets_init(&reader, (FlacInput){read_file, file, (uint32_t)length});
    for (;;) {
        OggPacket packet;
        OggPacketResult result =
            ogg_packets_next(&reader, packet_buffer, sizeof packet_buffer, &packet);
        if (result != OGG_PACKET_READY) {
            return result == OGG_PACKET_END;
        }
        uint8_t flags = (uint8_t)((packet.first ? 1 : 0) | (packet.last ? 2 : 0));
        if (!write_number(output, packet.bytes, 4) || !write_number(output, flags, 4) ||
            !write_number(output, packet.granule, 8) ||
            fwrite(packet_buffer, 1, packet.bytes, output) != packet.bytes) {
            return false;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) {
        return 1;
    }
    FILE *output = fopen(argv[2], "wb");
    bool success = output != NULL && write_packets(input, output);
    if (output != NULL && fclose(output) != 0) {
        success = false;
    }
    if (fclose(input) != 0) {
        success = false;
    }
    return success ? 0 : 1;
}
