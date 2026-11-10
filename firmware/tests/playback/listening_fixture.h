#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playback/listening_export.h"
#include "playback/listening_log.h"

enum {
    LISTENING_FIXTURE_SONGS = 6,
    LISTENING_FIXTURE_UNTITLED = 5,
    LISTENING_FIXTURE_MONDAY = 20766,
    LISTENING_FIXTURE_DAY = 86400
};

typedef struct ListeningDisk {
    uint8_t bytes[LISTENING_LOG_FILE_BYTES];
    uint32_t cut_bytes;
    uint32_t largest_write;
    int cut_write;
    int blind_from_write;
    int reads;
    int writes;
    bool refuse_reads;
    bool unaligned_block;
} ListeningDisk;

typedef struct ListeningSeen {
    ListeningRecord first;
    ListeningRecord last;
    uint64_t listened_ms;
    uint32_t count;
    bool ordered;
} ListeningSeen;

void listening_disk_reset(ListeningDisk *disk);
StoreFile listening_disk_file(ListeningDisk *disk, uint32_t size);
ListeningLogConfig listening_disk_config(ListeningDisk *disk);
ListeningPlay listening_fixture_play(uint8_t song, uint32_t listened_ms);
ListeningPlay listening_fixture_dated(uint8_t song, uint32_t day, uint32_t second);
ListeningLookup listening_fixture_lookup(void);
uint16_t listening_fixture_read_all(const ListeningLog *log, ListeningSeen *seen);
