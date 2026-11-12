#include "hidden_scan.h"

#include "hal/storage.h"
#include "library_store.h"
#include "playback/mix_identity.h"

static uint32_t hidden_offset(uint16_t record)
{
    return HIDDEN_TRACKS_HEAD_BYTES + (uint32_t)record * HIDDEN_TRACKS_RECORD_BYTES;
}

static bool hidden_identity(uint16_t record, MixIdentity *identity)
{
    uint8_t bytes[HIDDEN_TRACKS_RECORD_BYTES];
    HalFile file;
    if (!hal_file_open(&file, HIDDEN_TRACKS_PATH)) {
        return false;
    }
    bool read = hal_file_read(file, hidden_offset(record), bytes, sizeof bytes);
    hal_file_close(&file);
    return read && mix_identity_decode(bytes, sizeof bytes, identity) != 0;
}

void hidden_scan_load(HiddenScan *scan)
{
    HiddenTracksHead head = {.magic = 0};
    HalFile file;
    scan->records = 0;
    if (hal_file_open(&file, HIDDEN_TRACKS_PATH)) {
        (void)hal_file_read(file, 0, &head, sizeof head);
        hal_file_close(&file);
    }
    uint16_t records = head.magic == HIDDEN_TRACKS_MAGIC && head.records <= HIDDEN_TRACKS_RECORDS
                           ? head.records
                           : 0;
    for (; scan->records < records; scan->records++) {
        MixIdentity identity;
        scan->keys[scan->records] =
            hidden_identity(scan->records, &identity) ? mix_identity_hash(&identity) : 0;
    }
}

bool hidden_scan_skips(void *context, const BuilderTrack *track)
{
    const HiddenScan *scan = context;
    MixIdentity identity = {.start_ms = track->start_ms, .end_ms = track->end_ms};
    if (scan->records == 0) {
        return false;
    }
    for (uint32_t index = 0; index < MIX_PATH_BYTES - 1 && track->path[index] != 0; index++) {
        identity.path[index] = track->path[index];
    }
    uint32_t key = mix_identity_hash(&identity);
    for (uint16_t record = 0; key != 0 && record < scan->records; record++) {
        MixIdentity listed;
        if (scan->keys[record] == key && hidden_identity(record, &listed) &&
            mix_identity_equal(&listed, &identity)) {
            return true;
        }
    }
    return false;
}

void hidden_scan_forget_runtime(void)
{
    uint32_t stale = 0;
    (void)hal_file_write_at(HIDDEN_TRACKS_PATH, sizeof(uint32_t), &stale, sizeof stale);
}
