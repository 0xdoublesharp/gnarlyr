#include "playback/store.h"

enum {
    PROGRESS_EXACT_BYTES = 0x1000000
};

uint8_t store_write_progress(const StoreWrite *job)
{
    uint32_t bytes = job->header.bytes;
    if (job->result == STORE_DONE) {
        return 100;
    }
    if (bytes == 0) {
        return 0;
    }
    uint32_t half = job->written.position / 2 + job->verified.position / 2;
    return (uint8_t)(bytes < PROGRESS_EXACT_BYTES ? half * 100 / bytes : half / (bytes / 100));
}
