#include "storage_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "../resident/storage_entry.h"
#include "device/image_crc.h"
#include "emmc.h"

#define MAIN_CODE_BASE 0x03050000u

static bool storage_code_ready;
extern const uint8_t device_code_end[];

static const StorageCodeApi *api(void)
{
    return (const StorageCodeApi *)(uintptr_t)(STORAGE_CODE_BASE + STORAGE_CODE_HEADER_BYTES);
}

static bool read_code(uint32_t first_sector, uint32_t size)
{
    uint8_t *target = (uint8_t *)(uintptr_t)STORAGE_CODE_BASE;
    uint32_t sectors = (size + 511) / 512;
    for (uint32_t done = 0; done < sectors; done += 8) {
        uint32_t count = sectors - done < 8 ? sectors - done : 8;
        if (!emmc_read(NULL, first_sector + done, count, target + done * 512)) {
            return false;
        }
    }
    return true;
}

static bool load_code(uint32_t slot, uint32_t main_crc)
{
    uint8_t header[512];
    uint32_t size = 0;
    uint32_t first = slot + STORAGE_CODE_IMAGE_OFFSET / 512;
    return emmc_read(NULL, first, 1, header) && storage_code_size(header, sizeof header, &size) &&
           read_code(first, size) &&
           storage_code_validate((const uint8_t *)(uintptr_t)STORAGE_CODE_BASE, size, main_crc);
}

void board_storage_code_start(void)
{
    storage_code_ready = false;
    EmmcInfo info;
    if (!emmc_start(&info)) {
        return;
    }
    uint32_t main_size = (uint32_t)((uintptr_t)device_code_end - MAIN_CODE_BASE);
    uint32_t main_crc = image_crc_update(0, (const uint8_t *)(uintptr_t)MAIN_CODE_BASE, main_size);
    bool valid = load_code(info.layout.slot_1, main_crc) || load_code(info.layout.slot_2, main_crc);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    storage_code_ready = valid;
}

TrackOrderLoad track_order_open(TrackOrder *order, TrackOrderConfig config)
{
    if (!storage_code_ready) {
        *order = (TrackOrder){.config = config, .needs_reload = true};
        return TRACK_ORDER_UNREADABLE;
    }
    return api()->order_open(order, config);
}

TrackOrderLoad track_order_open_at(TrackOrder *order, TrackOrderConfig config, uint32_t generation)
{
    if (!storage_code_ready) {
        *order = (TrackOrder){.config = config, .needs_reload = true};
        return TRACK_ORDER_UNREADABLE;
    }
    return api()->order_open_at(order, config, generation);
}

bool track_order_read(const TrackOrder *order, uint16_t first, uint16_t count, uint16_t *tracks)
{
    return storage_code_ready && api()->order_read(order, first, count, tracks);
}

TrackOrderSource track_order_source(const TrackOrder *order)
{
    return storage_code_ready ? api()->order_source(order) : (TrackOrderSource){0};
}

bool track_order_job_begin(TrackOrderJob *job, TrackOrder *order, TrackOrderSource source,
                           TrackOrderEdit edit)
{
    return storage_code_ready && api()->order_begin(job, order, source, edit);
}

TrackOrderResult track_order_job_step(TrackOrderJob *job)
{
    return storage_code_ready ? api()->order_step(job) : TRACK_ORDER_FAILED;
}

void track_order_job_abort(TrackOrderJob *job)
{
    if (storage_code_ready) {
        api()->order_abort(job);
    }
}

uint8_t track_order_job_progress(const TrackOrderJob *job)
{
    return storage_code_ready ? api()->order_progress(job) : 0;
}

SessionLoad session_journal_open(SessionJournal *journal, SessionJournalConfig config,
                                 SessionState *state)
{
    if (!storage_code_ready) {
        *journal = (SessionJournal){.config = config, .needs_reload = true};
        return SESSION_UNREADABLE;
    }
    return api()->session_open(journal, config, state);
}

SessionLoad session_journal_previous(SessionJournal *journal, SessionState *state)
{
    return storage_code_ready ? api()->session_previous(journal, state) : SESSION_UNREADABLE;
}

SessionSave session_journal_save(SessionJournal *journal, const SessionState *state)
{
    return storage_code_ready ? api()->session_save(journal, state) : SESSION_SAVE_FAILED;
}
