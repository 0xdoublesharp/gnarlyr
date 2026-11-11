#include <stdio.h>

#include "library/track_tags.h"

typedef struct Input {
    FILE *file;
    uint32_t size;
} Input;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t length)
{
    Input *input = context;
    return offset <= input->size && length <= input->size - offset &&
           fseek(input->file, (long)offset, SEEK_SET) == 0 &&
           fread(out, 1, length, input->file) == length;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 3;
    }
    long length = ftell(file);
    if (length < 0 || (uint64_t)length > UINT32_MAX) {
        (void)fclose(file);
        return 3;
    }
    Input input = {.file = file, .size = (uint32_t)length};
    TrackTags tags;
    bool read = track_tags_read((ByteSource){read_file, &input}, input.size,
                                track_tags_format_for_name(argv[1]), &tags);
    (void)fclose(file);
    if (!read) {
        return 4;
    }
    printf("%u %u %u %u\n", (unsigned)tags.replay_gain.track, (unsigned)tags.replay_gain.album,
           (unsigned)tags.replay_gain.track_safe, (unsigned)tags.replay_gain.album_safe);
    return 0;
}
