#include "device/slot_copy.h"

#include <stddef.h>

#include "core/bytes.h"
#include "core/seam.h"

enum {
    SECTOR_BYTES = 512,
    CHUNK_SECTORS = UPDATER_CHUNK_BYTES / SECTOR_BYTES,
    CHUNK_WORDS = UPDATER_CHUNK_BYTES / 4,
    FW_SIZE = 0x44C000,
    FW_SIZE_OFFSET = 0x1F4,
    MAGIC_OFFSET = 0x1F8,
    HEADER_WORDS = SECTOR_BYTES / 4
};

static const uint8_t MAGIC[8] = {'R', 'K', 'n', 'a', 'n', 'o', 'F', 'W'};

static bool same_words(const uint32_t *left, const uint32_t *right, uint32_t words)
{
    for (uint32_t index = 0; index < words; index++) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool read_slot(const UpdateStorage *storage, uint32_t lba, uint32_t count, uint32_t *words)
{
    return storage->read(storage->context, lba, count, words);
}

static bool read_source(const SlotCopyJob *job, uint32_t chunk)
{
    if (job->file != NULL) {
        return job->file->read(job->file->context, chunk * UPDATER_CHUNK_BYTES, job->buffer,
                               UPDATER_CHUNK_BYTES);
    }
    return read_slot(job->storage, job->from + chunk * CHUNK_SECTORS, CHUNK_SECTORS, job->buffer);
}

static bool write_checked(const SlotCopyJob *job, uint32_t chunk)
{
    uint32_t lba = job->to + chunk * CHUNK_SECTORS;
    uint32_t *back = job->buffer + CHUNK_WORDS;
    return job->storage->write(job->storage->context, lba, CHUNK_SECTORS, job->buffer) &&
           read_slot(job->storage, lba, CHUNK_SECTORS, back) &&
           same_words(job->buffer, back, CHUNK_WORDS);
}

static bool copy_chunk(const SlotCopyJob *job, uint32_t chunk)
{
    uint32_t *target = job->buffer + CHUNK_WORDS;
    if (!read_source(job, chunk) ||
        !read_slot(job->storage, job->to + chunk * CHUNK_SECTORS, CHUNK_SECTORS, target)) {
        return false;
    }
    return same_words(job->buffer, target, CHUNK_WORDS) || write_checked(job, chunk);
}

static void report(const SlotCopyJob *job, uint32_t done)
{
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, done, job->chunks);
    }
}

CORE_SEAM bool slot_copy_run(const SlotCopyJob *job)
{
    for (uint32_t index = 0; index < CHUNK_WORDS; index++) {
        job->buffer[index] = 0xFFFFFFFFu;
    }
    if (!write_checked(job, 0)) {
        return false;
    }
    for (uint32_t chunk = 1; chunk < job->chunks; chunk++) {
        if (!copy_chunk(job, chunk)) {
            return false;
        }
        report(job, chunk);
    }
    if (!copy_chunk(job, 0)) {
        return false;
    }
    report(job, job->chunks);
    return true;
}

static bool header_is_ours(const uint8_t *bytes)
{
    for (uint32_t index = 0; index < sizeof MAGIC; index++) {
        if (bytes[MAGIC_OFFSET + index] != MAGIC[index]) {
            return false;
        }
    }
    return bytes_le32(bytes + FW_SIZE_OFFSET) == FW_SIZE;
}

bool slot_copy_is_valid(const UpdateStorage *storage, uint32_t slot, uint32_t *buffer)
{
    return read_slot(storage, slot, 1, buffer) && header_is_ours((const uint8_t *)buffer) &&
           read_slot(storage, slot + FW_SIZE / SECTOR_BYTES, 1, buffer + HEADER_WORDS) &&
           same_words(buffer, buffer + HEADER_WORDS, HEADER_WORDS);
}
