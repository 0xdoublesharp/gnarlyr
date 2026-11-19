#include "gfx/art_codec.h"

typedef struct BenchJob {
    const uint8_t *payload;
    const uint32_t *table;
    const ArtPalette *palette;
    Color *out;
    int width;
    int height;
    int from;
    int to;
    int codec;
    int alpha;
} BenchJob;

int bench_run(const BenchJob *job);

int bench_run(const BenchJob *job)
{
    for (int y = 0; y < job->height; y++) {
        ArtRow row = {job->payload + job->table[y], job->payload + job->table[y + 1], job->width};
        ArtSpan span = {job->out, job->from, job->to};
        bool done = job->codec == ART_CODEC_INDEXED ? art_row_indexed(&row, job->palette, span)
                                                    : art_row_lossless(&row, job->alpha != 0, span);
        if (!done) {
            return -1 - y;
        }
    }
    return 0;
}
