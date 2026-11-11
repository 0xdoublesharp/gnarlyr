#include "playback/listening_log.h"

#include <stddef.h>

#include "listening_root.h"

StoreResult listening_log_clear(ListeningLog *log)
{
    if (log->needs_reload) {
        ListeningTotals session = log->session;
        StoreFile file = log->file;
        bool enabled = log->enabled;
        *log = (ListeningLog){.file = file, .root = {.next = 1}, .enabled = enabled};
        StoreLoad load = file_valid(&log->file) ? load_root(log) : STORE_UNREADABLE;
        log->needs_reload = store_blocking(load);
        log->session = session;
        if (store_blocking(load)) {
            return STORE_FAILED;
        }
    }
    if (log->file.write == NULL || log->file.size < LISTENING_LOG_FILE_BYTES) {
        return STORE_FAILED;
    }
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    uint8_t bank = log->valid ? (uint8_t)(1 - log->bank) : 0;
    StoreHeader header = {.generation = log->valid ? log->generation + 1 : 1, .bytes = ROOT_BYTES};
    StoreSlot slot = {
        .file = &log->file, .kind = &KIND, .offset = (uint32_t)bank * STORE_BLOCK_BYTES};
    store_put32(block + STORE_FIELD_BYTES + ROOT_NEXT, log->root.next);
    store_encode(&KIND, &header, block);
    StoreResult result = store_publish(&slot, block, log->valid ? &log->generation : NULL);
    log->needs_reload = result == STORE_UNCERTAIN;
    if (result == STORE_DONE) {
        log->root = (ListeningRoot){.next = log->root.next};
        log->session = (ListeningTotals){0};
        log->generation = header.generation;
        log->bank = bank;
        log->valid = true;
    }
    return result;
}
