#include "listening_fixture.h"

#include <string.h>

typedef struct Song {
    ListeningTrack track;
    uint32_t length_ms;
    const char *artist;
    const char *album;
    const char *title;
    const char *recording;
    uint16_t number;
} Song;

static const Song SONGS[LISTENING_FIXTURE_SONGS] = {
    {{0x1001, 0xA1, 0xB1, 10}, 254000, "Björk", "Homogenic", "Hunter", "", 1},
    {{0x1002, 0xA1, 0xB1, 11}, 305000, "Björk", "Homogenic", "Jóga", "", 2},
    {{0x1003, 0xA2, 0xB1, 20}, 328000, "Björk", "Vespertine", "Hidden Place", "", 1},
    {{0x2001, 0xA3, 0xB2, 30},
     305000,
     "Portishead",
     "Dummy",
     "Roads",
     "0f1e2d3c-4b5a-6978-8695-a4b3c2d1e0f9",
     8},
    {{0x2002, 0xA3, 0xB2, 31}, 208000, "Portishead", "Dummy", "Glory Box", "", 11},
    {{0x3001, 0, 0xB3, 40}, 0, "Low", "", "Tab\tbed\nroom", "", 0},
};

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    ListeningDisk *disk = context;
    bool blind = disk->blind_from_write != 0 && disk->writes >= disk->blind_from_write;
    disk->reads++;
    if (disk->refuse_reads || blind || offset > sizeof disk->bytes ||
        length > sizeof disk->bytes - offset) {
        return false;
    }
    memcpy(bytes, disk->bytes + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    ListeningDisk *disk = context;
    if (offset > sizeof disk->bytes || length > sizeof disk->bytes - offset) {
        return false;
    }
    disk->writes++;
    disk->largest_write = length > disk->largest_write ? length : disk->largest_write;
    disk->unaligned_block |= length == STORE_BLOCK_BYTES && offset % STORE_BLOCK_BYTES != 0;
    bool cut = disk->writes == disk->cut_write;
    memcpy(disk->bytes + offset, bytes, cut && disk->cut_bytes < length ? disk->cut_bytes : length);
    return !cut;
}

void listening_disk_reset(ListeningDisk *disk)
{
    memset(disk, 0, sizeof *disk);
}

StoreFile listening_disk_file(ListeningDisk *disk, uint32_t size)
{
    return (StoreFile){.source = {read_disk, disk}, .write = write_disk, .size = size};
}

ListeningLogConfig listening_disk_config(ListeningDisk *disk)
{
    return (ListeningLogConfig){.file = listening_disk_file(disk, sizeof disk->bytes),
                                .enabled = true};
}

ListeningPlay listening_fixture_play(uint8_t song, uint32_t listened_ms)
{
    const Song *source = &SONGS[song];
    bool whole = source->length_ms != 0 && listened_ms >= source->length_ms;
    return (ListeningPlay){.track = source->track,
                           .length_ms = source->length_ms,
                           .listened_ms = listened_ms,
                           .end_ms = listened_ms,
                           .end = whole ? LISTENING_FINISHED : LISTENING_SKIPPED,
                           .output = LISTENING_OUTPUT_SINGLE_ENDED,
                           .format = LISTENING_FORMAT_LOSSLESS};
}

ListeningPlay listening_fixture_dated(uint8_t song, uint32_t day, uint32_t second)
{
    ListeningPlay play = listening_fixture_play(song, SONGS[song].length_ms);
    play.clock_s = day * LISTENING_FIXTURE_DAY + second;
    return play;
}

static void see(void *context, const ListeningRecord *record)
{
    ListeningSeen *seen = context;
    if (seen->count == 0) {
        seen->first = *record;
        seen->ordered = true;
    } else if (record->sequence <= seen->last.sequence) {
        seen->ordered = false;
    }
    seen->last = *record;
    seen->listened_ms += record->play.listened_ms;
    seen->count++;
}

uint16_t listening_fixture_read_all(const ListeningLog *log, ListeningSeen *seen)
{
    ListeningCursor cursor;
    memset(seen, 0, sizeof *seen);
    listening_cursor_begin(&cursor, log, LISTENING_SECTOR_RECORDS);
    for (int step = 0; step <= LISTENING_LOG_CAPACITY; step++) {
        if (listening_cursor_step(&cursor, (ListeningVisitor){see, seen}) != STORE_WORKING) {
            break;
        }
    }
    return cursor.damaged;
}
