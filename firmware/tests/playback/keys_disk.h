#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/track_keys.h"

typedef struct KeysDisk {
    uint8_t bytes[TRACK_KEYS_FILE_BYTES];
    int reads;
    int writes;
    int cut_write;
    int refuse_reads_after;
} KeysDisk;

void keys_disk_reset(KeysDisk *disk);
StoreFile keys_disk_file(KeysDisk *disk);
void keys_disk_publish(KeysDisk *disk, const TrackKey *entries, uint32_t count, uint16_t latest);
